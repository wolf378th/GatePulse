# GatePulse Project

## Overview
GatePulse is a dual-device Arduino-based gate monitoring system using NRF24L01 wireless communication.

The system consists of:

- **Box A (Transmitter):** Detects gate state (open/closed) and transmits status updates.
- **Box B (Receiver):** Receives gate status and triggers LED and buzzer alerts.

---

## Hardware Configuration

### Shared NRF24 Settings
- Address: `"GATEP"`
- Channel: `108`
- Data Rate: `RF24_250KBPS`
- Power Level: `RF24_PA_HIGH`
- CE Pin: `2`
- CSN Pin: `3`

---

## Box A – Transmitter

### Function
- Detects gate state using a limit switch (INPUT_PULLUP)
- Sends:
  - `"Gate Open"`
  - `"Gate Closed"`
- Stores last known state in EEPROM
- Sends periodic heartbeat messages
- Uses ACK-based transmission with retry logic

### Features
- Debounce handling (500 ms)
- EEPROM state recovery after power loss
- Retry mechanism (1 second interval)
- Heartbeat every 30 seconds
- NRF auto-recovery after repeated send failures

---

## Box B – Receiver

### Function
- Receives messages from Box A
- Controls:
  - LED indicator (active-low)
  - Buzzer alert system

### Features
- Two modes:
  - Mode 1: LED + buzzer
  - Mode 2: Silent (LED only)
- LED flashes when gate is open
- Buzzer sequence:
  - 3 initial beeps on gate open
  - Reminder beeps every 2 minutes
- Automatically clears alarm on `"Gate Closed"` message

---

## Communication Logic

- Box A transmits messages using NRF24L01 with auto-ack enabled
- Box B listens continuously and processes incoming messages
- Heartbeat messages ensure state synchronization
- System is designed to self-correct in case of missed transmissions

---

## Known Issue Under Investigation

- Box B does not trigger when gate opens after recent updates to Box A
- Changes include NRF auto-recovery logic on transmitter
- Need to verify:
  - Message transmission integrity
  - ACK handling behavior
  - Timing between transmit and receive

---

## Goal

Identify and resolve communication failure while preserving all existing functionality.
