#ifndef NETWORKING_H
#define NETWORKING_H

#include <Arduino.h>


class Networking {
public:
    void writeDataToThingSpeak(String data);
    void setup();
    bool isConnected();
};

#endif // NETWORKING_H