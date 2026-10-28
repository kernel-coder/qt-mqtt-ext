# Add MQTT to a Qt application with qt-mqtt-ext

qt-mqtt-ext is a C++ library that gives Qt 6 applications an MQTT client with a Qt-style API: a `QObject` with signals, properties and plain value types. Under the hood it builds and statically links Eclipse Paho MQTT C, so your code never touches Paho types. Use it on desktop, Android and embedded Linux when you need publish/subscribe messaging over TCP, TLS or WebSockets without depending on the GPLv3/commercial Qt MQTT module.

**Audience:** C++ developers who build Qt 6 applications with CMake and want to connect them to an MQTT broker (Mosquitto, AWS IoT Core, or any MQTT 3.1.1 broker).

**Before you begin:**
- CMake 3.16 or later.
- Qt 6.4 or later (Qt Core). The tests also need Qt Network and Qt Test. The example simulators need Qt 6.5 or later with Qt Quick.
- A C++17 compiler.
- Internet access the first time you configure, so CMake can download Paho. To build offline, see [Build without internet access](#build-without-internet-access).
- OpenSSL headers and libraries for your target, if you want TLS. See [Provide OpenSSL for your platform](#provide-openssl-for-your-platform).
- Docker, if you want to run the local broker, the tests or the simulators.

## Add the library to your CMake project

To use qt-mqtt-ext in an application, follow these steps:

1. Make the source available to your project, either as a git submodule or with `FetchContent`:

   ```cmake
   add_subdirectory(path/to/qt-mqtt-ext qt-mqtt-ext)
   ```

2. Link your target against the library:

   ```cmake
   target_link_libraries(MyApp PRIVATE MqttExt::MqttExt)
   ```

3. On Windows, copy the OpenSSL DLLs next to your executable at build time:

   ```cmake
   mqtt_ext_copy_runtime_dlls(MyApp)
   ```

   The function does nothing on other platforms or when OpenSSL is linked statically, so you can call it unconditionally.

4. Include the headers you need:

   ```cpp
   #include <MqttExt/Client.h>
   #include <MqttExt/Topic.h>
   ```

**Result:** your target links a static `MqttExt` library, which carries Paho and OpenSSL as private link dependencies.

**Note:** The tests build only when qt-mqtt-ext is the top-level CMake project. Set `MQTT_EXT_BUILD_TESTS=ON` to build them from a parent project.

## Provide OpenSSL for your platform

TLS (`ssl://`, `tls://`, `mqtts://` and `wss://`) needs OpenSSL. The build looks for it with CMake's standard `FindOpenSSL` module, and adds two defaults for the toolchains below.

| Platform | Default when you set nothing | To override |
|---|---|---|
| Windows, Qt's MinGW toolchain | `<mingw>/opt` next to the compiler, which Qt's MinGW package ships | `-DOPENSSL_ROOT_DIR=...` |
| Windows, MSVC | `<Qt>/Tools/OpenSSLv3/Win_x64`, the OpenSSL 3 toolkit from the Qt installer | `-DOPENSSL_ROOT_DIR=...` |
| macOS | Homebrew's `openssl@3` | `-DOPENSSL_ROOT_DIR=...` |
| Android | `${ANDROID_SDK_ROOT}/android_openssl/ssl_3` (the KDAB `android_openssl` layout Qt recommends) | `-DMQTT_EXT_ANDROID_OPENSSL_DIR=...` |
| Linux and embedded Linux | The system or sysroot OpenSSL | `-DOPENSSL_ROOT_DIR=...` |
| iOS | None | `OPENSSL_INCLUDE_DIR`, `OPENSSL_SSL_LIBRARY`, `OPENSSL_CRYPTO_LIBRARY` |

On Android the library links `libssl_3.so` and `libcrypto_3.so`, the same files Qt Network loads. Package them with your app through `QT_ANDROID_EXTRA_LIBS` if you don't already.

To build without TLS, configure with `-DMQTT_EXT_WITH_TLS=OFF`. Only `tcp://`, `mqtt://` and `ws://` addresses then work, and a TLS address fails with an `InvalidOptions` error.

**Caution:** The OpenSSL in Qt's MinGW 13.1 toolchain is version 1.1.1k. The 1.1.1 series stopped receiving security fixes in September 2023. It is fine for development builds. Ship Windows builds with a supported OpenSSL 3.x instead.

## Connect, subscribe and publish

A `Client` lives in one thread and must be used from that thread. Paho runs its network work on its own threads, and the library delivers every result to your thread through queued signals.

To connect and exchange messages, follow these steps:

1. Create a client and fill in the connection options:

   ```cpp
   auto* client = new MqttExt::Client(this);

   MqttExt::ConnectOptions options;
   options.serverUri = QStringLiteral("ssl://broker.example.com:8883");
   options.username = QStringLiteral("device-42");
   options.password = password;
   options.tls.caCertificates = caPem;
   options.will = MqttExt::Will { QStringLiteral("devices/42/status"), "offline", 1, true };
   client->setConnectOptions(options);
   ```

2. Subscribe and connect the signals. You can subscribe before you connect:

   ```cpp
   client->subscribe(QStringLiteral("devices/42/commands"), 1);

   connect(client, &MqttExt::Client::messageReceived, this, [](const MqttExt::Message& message) {
       qDebug() << message.topic << message.payload;
   });
   connect(client, &MqttExt::Client::stateChanged, this, [client](MqttExt::Client::State state) {
       if (state == MqttExt::Client::State::Connected)
           client->publish(QStringLiteral("devices/42/status"), "online", 1, true);
   });
   ```

3. Start the connection:

   ```cpp
   client->connectToBroker();
   ```

**Result:** the client connects, subscribes to every filter you registered, and keeps reconnecting with backoff if the connection drops.

`publish()` returns a token: a positive number for QoS 1 and 2, or `0` for QoS 0. It returns `-1` in these cases:
- The client is not connected.
- The topic, QoS or payload is invalid.
- `maxQueuedMessages` messages (default 1000) are already waiting to be written to the network.

The `published(token)` signal fires when the broker acknowledges a QoS 1 or 2 message, or when a QoS 0 message leaves the socket.

**Note:** The library does not queue messages while disconnected. Publish after `stateChanged(Connected)`, or keep your latest state and publish it on every reconnect, as step 2 does.

**Caution:** On slow hardware, such as a 32-bit ARM board, a tight loop of publishes can fill the queue faster than the network drains it. Treat `-1` as back-pressure: publish the latest state again later rather than every intermediate value.

## Handle connection state and failures

The client moves through four states: `Disconnected`, `Connecting`, `Connected` and `Disconnecting`. `Connecting` covers both an attempt in progress and the wait before the next retry.

| Situation | What the client does |
|---|---|
| Options are invalid (bad address, a password without a user name, a missing certificate file) | Emits `connectFailed(InvalidOptions, reason)` and returns to `Disconnected`. It does not retry. |
| The broker refuses the connection | Emits `connectFailed` with `ProtocolVersionRejected`, `ClientIdRejected`, `ServerUnavailable`, `BadCredentials` or `NotAuthorized`. |
| The network or TLS handshake fails | Emits `connectFailed(Transport, reason)`. |
| An established connection drops | Emits `connectionLost(cause)` and stays in `Connecting` while it retries. |
| Any failure while `autoReconnect` is on | Retries after `reconnectMinDelayMs`, doubling up to `reconnectMaxDelayMs`, with up to 20% random jitter. |
| The broker refuses a subscription | Emits `subscriptionFailed(filter, reason)` and stops tracking that filter. |

The client re-sends every tracked subscription after each reconnect, so you subscribe once.

To change credentials while the client retries, for example after a `BadCredentials` failure, call `setConnectOptions()` with new values. The next attempt uses them. A changed address or client ID takes effect on the next connection.

**Note:** `disconnectFromBroker()` and destroying a connected `Client` both close the session cleanly, so the broker does not publish the will message. The will reaches subscribers only when the connection drops without a clean close, such as a crash, a network loss or a session takeover.

**Tip:** Enable the `mqttext` logging category for connection details: `QT_LOGGING_RULES="mqttext.debug=true"`.

## Secure the connection with TLS

To connect over TLS, use an `ssl://`, `mqtts://` or `wss://` address and set `options.tls`:

| Field | Purpose |
|---|---|
| `caCertificates` | PEM bytes of the certificate authorities to trust, for example read from a Qt resource. The library writes them to a private temporary file, because OpenSSL needs a path. |
| `caCertificatesFile` | Path to a PEM file. Set this or `caCertificates`, not both. |
| `clientCertificateFile`, `privateKeyFile` | Client certificate and key for mutual TLS, as used by AWS IoT Core. Set both or neither. |
| `verifyPeer` | Checks the server certificate and host name. Default `true`. |

If you set neither CA field, OpenSSL uses its default trust store. That works on most Linux systems. On Android, iOS and Windows, OpenSSL has no usable default store, so pass the CA certificates.

**Caution:** Set `verifyPeer` to `false` only for local testing. Without verification, anyone on the network path can impersonate the broker.

## Connect to AWS IoT Core

AWS IoT Core is a standard MQTT broker, so the same client works with it. Use these settings:

- Address `ssl://<your-endpoint>.iot.<region>.amazonaws.com:8883`.
- `clientCertificateFile` and `privateKeyFile` set to the device certificate and key, and the Amazon root CA in `caCertificates`.
- A `clientId` that your IoT policy allows, usually the thing name.
- QoS 0 or 1 only. AWS IoT Core does not support QoS 2.

**Note:** Phone apps on AWS often authenticate through a custom authorizer on port 443. That route needs TLS ALPN, which this library does not expose yet.

## Run the local broker

The repository includes a Mosquitto 2.0 broker in Docker for development and testing. It requires a user name and password, applies a per-user access list, and listens only on `127.0.0.1`.

To start it, follow these steps:

1. From the repository root, run:

   ```shell
   docker compose -f broker/compose.yaml up -d
   ```

   The first start generates a certificate authority, a server certificate for `localhost`, `mosquitto` and `127.0.0.1`, and a random password for each user. They go into `broker/.secrets/`, which git ignores.

2. On Linux, take ownership of the generated files, which the containers create as root:

   ```shell
   sudo chown -R "$(id -u):$(id -g)" broker/.secrets
   ```

3. Read the passwords from `broker/.secrets/credentials.txt` and the CA from `broker/.secrets/certs/ca.crt`.

**Result:** the broker listens on these local ports:

| Port | Protocol |
|---|---|
| 18830 | MQTT over TCP |
| 18883 | MQTT over TLS 1.2 or later |
| 19001 | MQTT over WebSockets |
| 19443 | MQTT over secure WebSockets |

The broker has three users:

| User | Can publish to | Can read from |
|---|---|---|
| `tester` | `mqtt-ext-test/#` | `mqtt-ext-test/#` |
| `device-sim` | `devices/+/live`, `devices/+/status` | nothing |
| `app-sim` | nothing | `devices/+/live`, `devices/+/status` |

To use other ports, set `MQTT_EXT_TCP_PORT`, `MQTT_EXT_TLS_PORT`, `MQTT_EXT_WS_PORT` or `MQTT_EXT_WSS_PORT` before you start the broker. To create new credentials and certificates, stop the broker and delete `broker/.secrets/`.

**Note:** This broker's access list filters what a client receives, but it accepts every subscription. To exercise subscription refusals, the tests use a small scripted broker instead.

## Run the tests

The tests have two parts:
- `TopicTest` checks topic validation and wildcard matching. It needs no broker.
- `ClientTest` runs the client against the local broker over TCP, TLS, WebSockets and secure WebSockets. It covers reconnects, will messages, refusals, retained messages and deleting a client while messages arrive.

To run them, follow these steps:

1. Start the local broker as described in [Run the local broker](#run-the-local-broker).
2. Configure and build with the tests enabled:

   ```shell
   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<path-to-Qt>
   cmake --build build
   ```

3. Run the tests:

   ```shell
   ctest --test-dir build --output-on-failure
   ```

**Note:** `ClientTest` skips its broker tests when `broker/.secrets/credentials.txt` does not exist. If the file exists but the broker is down, the tests fail rather than skip.

To run `ClientTest` against a broker on another host or other ports, set `MQTT_EXT_TEST_HOST`, `MQTT_EXT_TEST_TCP_PORT`, `MQTT_EXT_TEST_TLS_PORT`, `MQTT_EXT_TEST_WS_PORT`, `MQTT_EXT_TEST_WSS_PORT` and `MQTT_EXT_TEST_SECRETS_DIR`.

## Test on Linux, including ARM

`scripts/test-linux.sh` builds the library and runs both test suites inside an Ubuntu 24.04 container with Qt 6.4 and OpenSSL 3. It runs on any machine with Docker, including Windows through Git Bash.

To test on Linux, follow these steps:

1. Start the local broker.
2. To test on ARM from an x86 machine, enable ARM emulation in Docker once:

   ```shell
   docker run --rm --privileged tonistiigi/binfmt --install arm64,arm
   ```

3. Run the script with the platform you want: `linux/amd64`, `linux/arm64` or `linux/arm/v7`:

   ```shell
   scripts/test-linux.sh linux/arm/v7
   ```

**Result:** CTest reports both test suites as passed for that architecture.

**Note:** The script checks the code on each architecture. It does not replace a build with your device's own cross-compiler and sysroot.

## Check every platform in CI

`.github/workflows/ci.yml` runs on every push and pull request on GitHub:

| Job | What it does |
|---|---|
| Linux amd64, arm64, armv7 | Runs `scripts/test-linux.sh` against the local broker. ARM runs under emulation. |
| Windows MinGW | Builds with Qt's MinGW toolchain and runs the tests that need no broker. |
| macOS | Builds against Homebrew OpenSSL and runs the tests that need no broker. |
| iOS | Builds the library without TLS, because the runner has no OpenSSL for iOS. |
| Android arm64 | Builds the library and tests against the KDAB OpenSSL build. |

Without a broker, `ClientTest` still checks option validation, every connection refusal code and a refused subscription against a scripted broker.

## Check which platforms are verified

These configurations pass both test suites as of October 2026:

| Platform | Toolchain | OpenSSL |
|---|---|---|
| Windows 11 | MinGW 13.1, Qt 6.11.1 | 1.1.1k |
| Ubuntu 24.04 x86_64 | GCC, Qt 6.4.2 | 3.0.13 |
| Ubuntu 24.04 aarch64 and armv7, emulated | GCC, Qt 6.4.2 | 3.0.13 |

The library and tests also build and link for Android arm64-v8a (NDK 29, Qt 6.11.1, OpenSSL 3.1.1). They have not run on an Android device.

macOS and iOS builds run only in the CI workflow. Windows MSVC has a default OpenSSL location, but no build or test covers it yet.

## Try the device and app simulators

Two small Qt Quick applications in `examples/` show the library in a real exchange:

- **Device simulator** acts like a thermostat. It publishes its readings as JSON to `devices/<serial>/live` whenever they change. It also publishes `online` to `devices/<serial>/status`, and sets `offline` as its will message.
- **App simulator** subscribes to every device and shows each one's status, readings, time since the last update and delivery latency.

Both connect over TLS by default. They read their passwords and the CA from `broker/.secrets/`.

To run them, follow these steps:

1. Start the local broker.
2. Open `examples/device-simulator/CMakeLists.txt` and `examples/app-simulator/CMakeLists.txt` in Qt Creator, or build them with CMake as separate projects.
3. Run both applications and click **Connect** in each. To connect at startup, pass `--connect`. To make the room temperature change on its own, pass `--drift` to the device simulator.
4. Move a slider in the device simulator.

**Result:** the app simulator shows the new reading within milliseconds. Close the device simulator and its status turns `offline`. End its process abruptly and the broker publishes the `offline` will instead.

## Build without internet access

CMake downloads Paho MQTT C 1.3.16 on the first configure and checks its SHA-256 hash. To build offline, extract the same release and point CMake at it:

```shell
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_PAHO_MQTT_C=/path/to/paho.mqtt.c-1.3.16
```

## Related

- [Eclipse Paho MQTT C](https://github.com/eclipse-paho/paho.mqtt.c)
- [Eclipse Mosquitto](https://mosquitto.org/)
- [AWS IoT Core MQTT](https://docs.aws.amazon.com/iot/latest/developerguide/mqtt.html)
- [Qt for Android OpenSSL support](https://doc.qt.io/qt-6/android-openssl-support.html)

## Meet the third-party license terms

Binaries built with qt-mqtt-ext contain Eclipse Paho MQTT C, available under the Eclipse Public License 2.0 or the Eclipse Distribution License 1.0. When you distribute such a binary, include the Paho license notice. Builds with TLS also use OpenSSL, under the license of the OpenSSL version you link.
