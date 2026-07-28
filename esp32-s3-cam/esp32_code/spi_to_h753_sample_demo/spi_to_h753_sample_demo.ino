#include <Arduino.h>
#include <SPI.h>

#define SPI_SCK   1
#define SPI_MISO  2
#define SPI_MOSI  3
#define SPI_CS    14

#define SPI_PACKET_SIZE 32

SPIClass h753SPI(FSPI);

uint8_t txBuffer[SPI_PACKET_SIZE];
uint8_t rxBuffer[SPI_PACKET_SIZE];

void setup()
{
    Serial0.begin(115200);
    delay(2000);

    pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS, HIGH);

    h753SPI.begin(
        SPI_SCK,
        SPI_MISO,
        SPI_MOSI,
        SPI_CS
    );

    memset(txBuffer, 0x55, sizeof(txBuffer));
    memset(rxBuffer, 0x00, sizeof(rxBuffer));

    Serial0.println();
    Serial0.println("ESP32-S3 SPI master started");
    Serial0.println("SCK  = GPIO1");
    Serial0.println("MISO = GPIO47");
    Serial0.println("MOSI = GPIO3");
    Serial0.println("CS   = GPIO14");
}

void loop()
{
    memset(rxBuffer, 0x00, sizeof(rxBuffer));

    h753SPI.beginTransaction(
        SPISettings(
            100000,
            MSBFIRST,
            SPI_MODE0
        )
    );

    digitalWrite(SPI_CS, LOW);

    /*
     * 給 STM32 Slave 一點時間偵測 NSS LOW。
     */
    delayMicroseconds(100);

    h753SPI.transferBytes(
        txBuffer,
        rxBuffer,
        SPI_PACKET_SIZE
    );

    delayMicroseconds(100);

    digitalWrite(SPI_CS, HIGH);

    h753SPI.endTransaction();

    Serial0.print("ESP32 transmitted: ");

    for (int i = 0; i < SPI_PACKET_SIZE; i++)
    {
        Serial0.printf("%02X ", txBuffer[i]);
    }

    Serial0.println();

    Serial0.print("ESP32 received:    ");

    for (int i = 0; i < SPI_PACKET_SIZE; i++)
    {
        Serial0.printf("%02X ", rxBuffer[i]);
    }

    Serial0.println();

    bool receivePass = true;

    for (int i = 0; i < SPI_PACKET_SIZE; i++)
    {
        if (rxBuffer[i] != 0xA5)
        {
            receivePass = false;
            break;
        }
    }

    if (receivePass)
    {
        Serial0.println("RX check: PASS, all bytes are A5");
    }
    else
    {
        Serial0.println("RX check: FAIL, data is not all A5");
    }

    Serial0.println();

    delay(1000);
}