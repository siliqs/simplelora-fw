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

void setup()
{
    siliqs_esp32_setup(SQ_INFO);

    if (!loraService.begin())
    {
        Serial.println(F("Failed to initialize LoRa!"));
        while (true)
        {
            delay(1000);
        }
    }

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
