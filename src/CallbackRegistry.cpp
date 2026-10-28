#include "CallbackRegistry.h"

#include "MqttExt/Client.h"

#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>

namespace MqttExt {

namespace {

// Paho invokes callbacks on its own threads and gives no guarantee that one has finished before
// MQTTAsync_destroy returns. Callbacks therefore carry an opaque key instead of a pointer, and the
// key is looked up under this lock. The registry is never destroyed so that a Paho thread still
// running during static destruction cannot touch a dead mutex.
struct Registry
{
    QMutex mutex;
    QHash<quintptr, Client*> clients;
    quintptr lastKey = 0;
};

Registry& registry()
{
    static Registry* instance = new Registry;
    return *instance;
}

}

quintptr CallbackRegistry::add(Client* client)
{
    Registry& r = registry();
    QMutexLocker locker(&r.mutex);
    if (++r.lastKey == 0)
        ++r.lastKey;
    r.clients.insert(r.lastKey, client);
    return r.lastKey;
}

void CallbackRegistry::remove(quintptr key)
{
    Registry& r = registry();
    QMutexLocker locker(&r.mutex);
    r.clients.remove(key);
}

void CallbackRegistry::post(quintptr key, std::function<void(Client*)> handler)
{
    Registry& r = registry();
    QMutexLocker locker(&r.mutex);
    Client* client = r.clients.value(key, nullptr);
    if (!client)
        return;
    QMetaObject::invokeMethod(client, [client, handler = std::move(handler)]() { handler(client); }, Qt::QueuedConnection);
}

void* CallbackRegistry::toContext(quintptr key)
{
    return reinterpret_cast<void*>(key);
}

quintptr CallbackRegistry::fromContext(void* context)
{
    return reinterpret_cast<quintptr>(context);
}

}
