"""Re-apply ESP32 setInsecure() support on ArduinoWebsockets after lib updates."""
Import("env")
from pathlib import Path

root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "ArduinoWebsockets"
tcp = root / "src/tiny_websockets/network/esp32/esp32_tcp.hpp"
ws = root / "src/websockets_client.cpp"

if tcp.exists():
    text = tcp.read_text()
    if "void setInsecure()" not in text:
        old = """  class SecuredEsp32TcpClient : public GenericEspTcpClient<WiFiClientSecure> {
  public:
    void setCACert(const char* ca_cert) {
      this->client.setCACert(ca_cert);
    }"""
        new = """  class SecuredEsp32TcpClient : public GenericEspTcpClient<WiFiClientSecure> {
  public:
    void setInsecure() {
      this->client.setInsecure();
    }

    void setCACert(const char* ca_cert) {
      this->client.setCACert(ca_cert);
    }"""
        if old in text:
            tcp.write_text(text.replace(old, new, 1))
            print("patched esp32_tcp.hpp setInsecure")

if ws.exists():
    text = ws.read_text()
    needle = """    #elif defined(ESP32)
        if(this->_optional_ssl_ca_cert) {
            client->setCACert(this->_optional_ssl_ca_cert);
        }
        if(this->_optional_ssl_client_ca) {"""
    repl = """    #elif defined(ESP32)
        if(this->_optional_ssl_ca_cert) {
            client->setCACert(this->_optional_ssl_ca_cert);
        } else {
            client->setInsecure();
        }
        if(this->_optional_ssl_client_ca) {"""
    if needle in text and "client->setInsecure();" not in text.split("elif defined(ESP32)")[1].split("#endif")[0]:
        ws.write_text(text.replace(needle, repl, 1))
        print("patched websockets_client.cpp ESP32 insecure")
