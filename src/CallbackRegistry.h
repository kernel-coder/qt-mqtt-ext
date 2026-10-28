#pragma once

#include <QtGlobal>

#include <functional>

namespace MqttExt {

class Client;

class CallbackRegistry
{
public:
    static quintptr add(Client* client);
    static void remove(quintptr key);
    static void post(quintptr key, std::function<void(Client*)> handler);

    static void* toContext(quintptr key);
    static quintptr fromContext(void* context);
};

}
