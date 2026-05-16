# NUCLEO-H753ZI-FINAL-PROJECT

This project is developed for the **STM32 NUCLEO-H753ZI** board.  
It focuses on building a secure embedded communication system that combines:

- STM32H753ZI firmware development
- AES-256-GCM encryption / decryption
- ML-KEM post-quantum key encapsulation
- UART communication
- Wi-Fi / Bluetooth module control through AT commands

The main purpose of this project is to test how encrypted data can be transmitted between embedded devices and external communication modules.

---

## Hardware Platform

### Main Board

- **Board:** ST NUCLEO-H753ZI
- **MCU:** STM32H753ZI
- **Core:** ARM Cortex-M7
- **Development Tool:** STM32CubeIDE

### Communication Modules

This project may use external Wi-Fi or Bluetooth modules controlled through UART and AT commands.

Example module command reference:

[Basic AT Command Set](https://aithinker-combo-guide-en.readthedocs.io/en/master/docs/command-set/AT_Basic_Commands.html)

---

## Project Library

The required NUCLEO-H753ZI project library can be downloaded from the following Google Drive folder:

[NUCLEO-H753ZI Library](https://drive.google.com/drive/folders/1NgRNA8Znc2Vhj98iJe--_e-DzBWLa1Uo)

This library contains the supporting files used for STM32H753ZI development, including cryptographic and communication-related source files.

---

## Cryptography

### AES-256-GCM

AES-256-GCM is used for authenticated encryption.  
It provides both:

- **Confidentiality**: protects the plaintext data
- **Authentication**: verifies that the ciphertext has not been modified

In this project, AES-GCM can be used to encrypt image bytes, sensor data, or other transmitted payloads.

### ML-KEM

ML-KEM is used for post-quantum key encapsulation.  
It allows two devices to securely establish a shared secret key, which can later be used as the AES-GCM encryption key.

Reference GitHub repository:

[ML-KEM / Kyber GitHub](https://github.com/pq-crystals/kyber/)

---

## Communication Design

The basic communication flow is:

```text
PC / Camera / Data Source
        |
        v
STM32H753ZI
        |
        | UART
        v
Wi-Fi / Bluetooth Module
        |
        v
Remote Device / Server