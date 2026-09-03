// simpleLoRa reference firmware — raw (non-LoRaWAN) point-to-point ping-pong over SX1262.
//
// Purpose: generate a real, non-LoRaWAN LoRa PHY payload on the air, to verify that a
// LoRaWAN-oriented gateway stack (chirpstack-concentratord) forwards it unmodified via its
// ZeroMQ event socket instead of requiring/expecting a valid LoRaWAN MAC frame. See the
// simpleLoRa gateway-side forwarder project for the receiving end of that verification.
//
// Payload format (deliberately simple, plain ASCII, pipe-delimited):
//   SIMPLELORA|<sender_node_id>|<sequence>
//
// Two boards, two roles (selected at build time — see platformio.ini):
//   node1 (SIMPLELORA_ROLE_INITIATOR) sends the first message.
//   node2 waits, then both sides just echo-and-increment forever.
//
// Two independent samples for exercising each direction of the gateway link (useful while
// debugging uplink/downlink issues, or as simple remote-control demos):
//
//   - The initiator (node1) sends
//       CNT|<sender_node_id>|<sequence>
//     once per USER_KEY button press (GPIO9) — a gateway-side tool (e.g. Node-RED) reacts to
//     each arrival (e.g. by publishing a downlink to another node) as an uplink-triggered
//     chain, or just counts them as a debug/health signal for the uplink path.
//   - Any board recognizes
//       LED|<target_node_id>|TOGGLE
//     flipping its own LED's current on/off state if <target_node_id> matches its
//     SIMPLELORA_NODE_ID. A gateway-side tool can publish this on receipt of the above (or
//     on its own timer) to exercise the downlink path — each receipt toggles the LED.
//
// Both are separate prefixes from SIMPLELORA|, so neither interferes with the ping-pong
// loop above.

#include "bsp.h"
#include "siliqs_esp32.h"

#ifndef SIMPLELORA_NODE_ID
#define SIMPLELORA_NODE_ID 0
#endif

lora_params_settings params = {
    .DIO1 = LORA_DIO1,
    .BUSY = LORA_BUSY,
    .NRST = LORA_NRST,
    .MISO = LORA_MISO,
    .MOSI = LORA_MOSI,
    .SCK = LORA_SCK,
    .NSS = LORA_NSS,
    .FREQUENCY = SIMPLELORA_FREQUENCY_MHZ,
    .BANDWIDTH = SIMPLELORA_BANDWIDTH_KHZ,
    .SF = SIMPLELORA_SF,
    .CR = SIMPLELORA_CR,
    .SYNC_WORD = SIMPLELORA_SYNC_WORD,
    .OUTPUT_POWER = SIMPLELORA_OUTPUT_POWER,
    .PREAMBLE_LENGTH = SIMPLELORA_PREAMBLE_LENGTH};

LoRaService loraService(&params);

static String buildMessage(int seq)
{
    return "SIMPLELORA|" + String(SIMPLELORA_NODE_ID) + "|" + String(seq);
}

// Pull the sequence number back out of an incoming (possibly foreign) message.
// Returns -1 if the message doesn't match our format.
static int parseSequence(const String &msg)
{
    if (!msg.startsWith("SIMPLELORA|"))
    {
        return -1;
    }
    int lastPipe = msg.lastIndexOf('|');
    if (lastPipe < 0)
    {
        return -1;
    }
    return msg.substring(lastPipe + 1).toInt();
}

static int nextSeq = 0;
static unsigned long lastSendMs = 0;

static void sendNext()
{
    String msg = buildMessage(nextSeq++);
    Serial.println("Sending: " + msg);
    loraService.sendMessage(msg);
    lastSendMs = millis();
}

static int buttonSeq = 0;

static void sendButtonUplink()
{
    String msg = "CNT|" + String(SIMPLELORA_NODE_ID) + "|" + String(buttonSeq++);
    Serial.println("Button pressed, sending: " + msg);
    loraService.sendMessage(msg);
}

// Format: LED|<target_node_id>|TOGGLE. Applies only if <target_node_id> matches this
// board's own SIMPLELORA_NODE_ID; otherwise it's silently not for us (not an error). Every
// matching command flips the LED from its current state rather than setting it to a fixed
// value — the third field's content isn't inspected, receiving the command at all is the
// trigger (matches a gateway-side tool sending the same fixed command on a timer).
static bool ledIsOn = false;

static void handleLedCommand(const String &msg)
{
    int firstPipe = msg.indexOf('|');
    int secondPipe = msg.indexOf('|', firstPipe + 1);
    if (firstPipe < 0 || secondPipe < 0)
    {
        return;
    }
    int targetId = msg.substring(firstPipe + 1, secondPipe).toInt();
    if (targetId != SIMPLELORA_NODE_ID)
    {
        return;
    }

    ledIsOn = !ledIsOn;
    Serial.println(ledIsOn ? "LED command: toggled ON" : "LED command: toggled OFF");
    if (ledIsOn)
    {
        led.on();
    }
    else
    {
        led.off();
    }
}

#ifdef SIMPLELORA_ROLE_INITIATOR
static int lastButtonState = HIGH;
static unsigned long lastButtonChangeMs = 0;
static const unsigned long BUTTON_DEBOUNCE_MS = 50;
#endif

void setup()
{
    siliqs_esp32_setup(SQ_INFO);

    led.begin(SIMPLELORA_LED_PIN, true /* activeLow */);

    if (!loraService.begin())
    {
        Serial.println(F("Failed to initialize LoRa!"));
        while (true)
        {
            delay(1000);
        }
    }

    // Explicit build marker — bump this string every time firmware behavior changes.
    // Ambiguous "is this old or new firmware actually running" questions have cost real
    // debugging time in this project — only a printed marker settles it beyond doubt.
    Serial.println(F("simpleLoRa build: button-diag-v2 (button-triggered CNT + GPIO9 toggle diagnostic)"));
    Serial.printf("simpleLoRa node %d ready — freq=%.1fMHz sf=%d bw=%.0fkHz\n",
                  SIMPLELORA_NODE_ID, SIMPLELORA_FREQUENCY_MHZ, SIMPLELORA_SF, SIMPLELORA_BANDWIDTH_KHZ);

    // LoRaService::begin() does not itself enter RX mode — it only starts listening as a
    // side effect of a *transmit* completing (see handleOperation() in lora_service.cpp).
    // A node that never transmits first (the responder) would otherwise never receive
    // anything at all. Explicitly arm the receiver here so both roles listen from boot.
    if (!loraService.start_receiving())
    {
        Serial.println(F("Failed to start receiving!"));
    }

#ifdef SIMPLELORA_ROLE_INITIATOR
    pinMode(SIMPLELORA_BUTTON_PIN, INPUT_PULLUP);
    // Read the actual pin state here rather than assuming HIGH — GPIO9 doubles as this
    // chip's boot-strap pin and can sit transiently LOW right after a reset/flash. Seeding
    // lastButtonState from a hardcoded HIGH caused a spurious "press" to be detected on the
    // very first loop() iteration whenever that happened.
    lastButtonState = digitalRead(SIMPLELORA_BUTTON_PIN);

    sendNext();
#else
    Serial.println("Waiting for first message...");
#endif
}

static unsigned long lastHeartbeatMs = 0;

void loop()
{
    if (millis() - lastHeartbeatMs > 5000)
    {
        lastHeartbeatMs = millis();
        Serial.printf("[heartbeat] node %d alive, uptime=%lus\n", SIMPLELORA_NODE_ID, millis() / 1000);
    }

#ifdef SIMPLELORA_ROLE_INITIATOR
    // USER_KEY button: external pull-up, idles HIGH, reads LOW while pressed.
    int buttonState = digitalRead(SIMPLELORA_BUTTON_PIN);
    if (buttonState != lastButtonState && millis() - lastButtonChangeMs > BUTTON_DEBOUNCE_MS)
    {
        lastButtonChangeMs = millis();
        lastButtonState = buttonState;

        // Diagnostic: toggle the LED and log on every GPIO9 transition (both press AND
        // release), independent of the "press -> send LoRa uplink" logic below. Watch the
        // physical LED, or this console over serial, to confirm the pin is actually
        // changing state at all when the button is pressed.
        ledIsOn = !ledIsOn;
        if (ledIsOn)
        {
            led.on();
        }
        else
        {
            led.off();
        }
        Serial.println(buttonState == LOW ? "GPIO9 toggled: now LOW (pressed)" : "GPIO9 toggled: now HIGH (released)");

        if (buttonState == LOW)
        {
            sendButtonUplink();
        }
    }
#endif

    String receivedMessage = loraService.getReceivedMessage();
    if (!receivedMessage.isEmpty())
    {
        Serial.println("Received: " + receivedMessage +
                        " ,RSSI: " + String(loraService.radio.getRSSI()) +
                        " ,SNR: " + String(loraService.radio.getSNR()));

        if (parseSequence(receivedMessage) >= 0)
        {
            delay(1000); // avoid immediate retransmission / collision
            sendNext();
        }
        else if (receivedMessage.startsWith("LED|"))
        {
            handleLedCommand(receivedMessage);
        }
        else
        {
            Serial.println("Received malformed/foreign message, ignoring.");
        }
    }

#ifdef SIMPLELORA_ROLE_INITIATOR
    // Beacon fallback: the initiator's job is to generate traffic for the gateway-side
    // passthrough test even if the two boards never hear each other directly (weak P2P
    // link, out of range, etc). Resend periodically if nothing has come back.
    if (millis() - lastSendMs > 8000)
    {
        sendNext();
    }
#endif

    delay(100);
}
