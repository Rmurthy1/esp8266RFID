#ifndef NETWORKING_H
#define NETWORKING_H

#include <Arduino.h>


class Networking {
public:
    void writeDataToThingSpeak(String data);
    void setup();
};

#endif // NETWORKING_H