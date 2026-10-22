#pragma once
// Host-test stand-in for src/NeuroSDK.hpp. It exposes the same static transport surface (one shared declaration list);
// FakeNeuro.cpp implements it by recording calls and enforcing the real transport preconditions. Nothing here talks to
// the engine or a socket.
#include "Actions/Action.hpp"
#include <string>
#include <vector>

class NeuroSDK {
public:
#include "NeuroSDKTransport.inc"
};
