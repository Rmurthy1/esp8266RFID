#ifndef NETWORKING_H
#define NETWORKING_H

#include <Arduino.h>


class Networking {
public:
    void writeDataToThingSpeak(String data);
    void setup();
    bool isConnected();
    void writeDataToFireBaseDatabase(String payload, String endpoint, bool &success);
};

#endif // NETWORKING_H