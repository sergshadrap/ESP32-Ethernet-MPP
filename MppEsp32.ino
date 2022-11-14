#include <Arduino.h>
#include <ETH.h>
#include <WiFiUdp.h> 


#define UDP_TX_PACKET_MAX_SIZE 2048
/*
   * ETH_CLOCK_GPIO0_IN   - default: external clock from crystal oscillator
   * ETH_CLOCK_GPIO0_OUT  - 50MHz clock from internal APLL output on GPIO0 - possibly an inverter is needed for LAN8720
   * ETH_CLOCK_GPIO16_OUT - 50MHz clock from internal APLL output on GPIO16 - possibly an inverter is needed for LAN8720
   * ETH_CLOCK_GPIO17_OUT - 50MHz clock from internal APLL inverted output on GPIO17 - tested with LAN8720
*/
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_OUT          // Version with PSRAM
//#define ETH_CLK_MODE    ETH_CLOCK_GPIO16_OUT            // Version with not PSRAM

// Pin# of the enable signal for the external crystal oscillator (-1 to disable for internal APLL source)
#define ETH_POWER_PIN   4

// Type of the Ethernet PHY (LAN8720 or TLK110)
#define ETH_TYPE        ETH_PHY_LAN8720

// I²C-address of Ethernet PHY (0 or 1 for LAN8720, 31 for TLK110)
#define ETH_ADDR        0

// Pin# of the I²C clock signal for the Ethernet PHY
#define ETH_MDC_PIN     23

// Pin# of the I²C IO signal for the Ethernet PHY
#define ETH_MDIO_PIN    18

// #define NRST            12

#define checkin 10000
const char* DeviceVersion = "Mpp32Relays 1.0.0";

    
static bool eth_connected = false;
WiFiServer server; // Declare server objects
WiFiUDP Udp;

String udn="",location,group;
String JSONReply="";
String BroadcastIP="239.255.255.250";
String Subscriber[4];
int Subscriber_num=0;

unsigned int localPort = 8898;      // local port to listen on
unsigned int BroadcastPort = 1900;
boolean device_state=false;
String Srelays="12,14,13,15";
String JsonRelays[10];
unsigned int PinRelays[10];
unsigned long lastnotify;
unsigned long next = millis();
static String UID;

const String& getUID() {
  if (UID.length() == 0) {
    UID = ETH.macAddress();
    while (UID.indexOf(':') > 0)
      UID.replace(":", "");
    UID.toLowerCase(); // compatible with V2
  }
  return UID;
}


String getDefaultUDN() {
  return String("MppSwitch") + "_" + getUID();
}

class MppTokens {
public:
  MppTokens(const String& string, char delim)
  {
    this->string = string;
    this->delim = delim;
  }

  const String next() {
    String result;
    if (index < string.length()) {
      int i = index;
      int j = string.indexOf(delim, index);
      if (j == -1) {
        index = string.length();
        result = string.substring(i);
      } else {
        index = j + 1;
        result = string.substring(i, j);
      }
    } else
      result = String();
    return result;
  }
private:
  String string;
  unsigned index = 0;
  char delim;
};


/*
void addSubscriber(const String& ip) {
  Subscription* subscription = NULL;
  for (int i = 0; i < count; i++) {
    if (strcmp(subscriptions[i].ip, ip.c_str()) == 0)
      subscription = &subscriptions[i];
  }
  if (subscription == NULL) {
    ++count;
    subscriptions = (Subscription*) realloc(subscriptions,
        count * sizeof(struct Subscription));
    subscription = &subscriptions[count - 1];
    strcpy(subscription->ip, ip.c_str());
    Serial.printf("added subscriber %s\n", subscription->ip);
  }
  subscription->expires = millis() + 1000 * 10 * 60; // 10m
  Serial.printf("%s subscribed until %lu\n", subscription->ip,
      subscription->expires);
}*/



boolean notifySubscribers(String IpClient,String JsonQuery) {
    unsigned long now = millis();
        if (now > lastnotify+600000) {  // notifying every 10 min
        Serial.printf("Notifying: %s  ...\n",IpClient.c_str());
        Udp.beginPacket(IpClient.c_str(), localPort);
          int result = Udp.write((const uint8_t *)JsonQuery.c_str(),JsonQuery.length());
          Udp.endPacket();
          Serial.println("Notifications sent.");
          lastnotify=millis();
          return true;
      }
  return false;
}

void SendBroadcastUDP()  {
for (int i = 0;; i++) {
  if (JsonRelays[i].length() == 0) break;  
 
  Udp.beginPacket(BroadcastIP.c_str(), BroadcastPort);
       int result = Udp.write((const uint8_t *)makeJsonString(i).c_str(),makeJsonString(i).length());
        Udp.endPacket();
    }
}


boolean CheckUdpDiscovery() {
  
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];
      int packetSize = Udp.parsePacket();
  if(packetSize)
  {
    Serial.print("Received packet of size ");
    Serial.println(packetSize);
    Serial.print("From ");
    IPAddress remote = Udp.remoteIP();
    for (int i =0; i < 4; i++)
    {
      Serial.print(remote[i], DEC);
      if (i < 3)
      {
        Serial.print(".");
      }
    }
    Serial.print(", port ");
    Serial.println(Udp.remotePort());

    // read the packet into packetBuffer
    int len = Udp.read(packetBuffer,UDP_TX_PACKET_MAX_SIZE);
    Serial.println("Contents:");
    Serial.println(packetBuffer);
    if (String(packetBuffer).startsWith("discover")) return true;
}
  return false;
} 


void WiFiEvent(WiFiEvent_t event)
{


    switch (event)
    {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("ETH Started");
        // set eth hostname here
        ETH.setHostname("MppEsp32Ethernet");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("ETH Connected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.print("ETH MAC: ");
        Serial.print(ETH.macAddress());
        Serial.print(", IPv4: ");
        Serial.print(ETH.localIP());
        
  //       SendBroadcastUDP();
        if (ETH.fullDuplex())
        {
            Serial.print(", FULL_DUPLEX");
        }
        Serial.print(", ");
        Serial.print(ETH.linkSpeed());
        Serial.println("Mbps");
        eth_connected = true;
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        eth_connected = false;
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        eth_connected = false;
        break;
    default:
        break;
    }
}

void UpdateStatus(WiFiClient& client)   {
  for (int i = 0;; i++) {
  if (JsonRelays[i].length() == 0)  break;
 String JSONR=JsonRelays[i].c_str();
// client.println(JSONR);
// client.println("\r\n\r\n");
     Udp.beginPacket(client.remoteIP().toString().c_str(), localPort);
        int result = Udp.write((const uint8_t *)JSONR.c_str(),JSONR.length());
        Udp.endPacket();
        
  }
}



String makeJsonArray() {
  JSONReply="";
//  JSONReply = "[{\"udn\":\"" + getDefaultUDN() +"\",\"name\":\"Mpp_ESP32_Eth\",\"location\":\"" +location+"\",\"group\":\""+ group +"\",\"state\":\"" +( device_state ? "on" : "off") + "\"}]";
JSONReply+="[";
for (int i = 0;; i++) {
  if (JsonRelays[i].length() == 0)  break;
   JSONReply+=JsonRelays[i];
   JSONReply+=",";
}
  if(JSONReply.endsWith(",")) JSONReply.remove(JSONReply.length()-1);
    JSONReply+="]";
      return JSONReply.c_str();  
}

String makeJsonString(unsigned Rnumber) {
  device_state=CheckRelayState(Rnumber);
  JSONReply = "{\"location\":\"" +location+"\",\"state\":\"" +( device_state ? "on" : "off") + "\",\"group\":\""+ group +"\",\"udn\":\"" + getDefaultUDN() +"_"+Rnumber+"\"}";
  return JSONReply;  
}


void sendDiscoveryResponse(IPAddress remoteIp, int remotePort) {
  Serial.printf("Responding to discovery request from %s:%d\n",remoteIp.toString().c_str(), remotePort);
  // send discovery reply
  Udp.beginPacket(remoteIp, remotePort);
 int result = Udp.write((const uint8_t *)makeJsonArray().c_str(),makeJsonArray().length());
  Udp.endPacket();
  Serial.printf("Sent discovery response to %s:%d (%d bytes sent)\n",
      remoteIp.toString().c_str(), remotePort, result);
}

int handleIncomingUdp(WiFiUDP &Udp) {
  char incoming[16]; // only "discover" is accepted
// receive incoming UDP packets
 Serial.printf("Received from %s, port %d\n", Udp.remoteIP().toString().c_str(), Udp.remotePort());
  int len = Udp.read(incoming, sizeof(incoming) - 1);
  if (len > 0) {
    incoming[len] = 0;
   Serial.printf("UDP packet contents: %s\n", incoming);
    if (String(incoming).startsWith("discover"))
      sendDiscoveryResponse(Udp.remoteIP(), Udp.remotePort());
  }
  return OK;
}


char ParseGet (String InputString)  {
  if(InputString.indexOf(getDefaultUDN())) {
    Serial.printf("String: %s  compare:%d  symbol:%c \n", InputString.c_str(), InputString.lastIndexOf(" HTTP/1.1"),InputString.charAt(InputString.lastIndexOf(" HTTP/1.1")-1) );
  return InputString.charAt(InputString.lastIndexOf(" HTTP/1.1")-1);
  }
  return -1;
 }

 char ParsePut (String InputString)  {
  if(InputString.indexOf(getDefaultUDN())) {
  return InputString.charAt(InputString.lastIndexOf("?state=")-1);
  }
  return -1;
 }

boolean CheckRelayState(int relaynum) {
  return digitalRead(PinRelays[relaynum]);
}

void SetRelayState(unsigned relaynum, boolean state)  {
  pinMode(PinRelays[relaynum],OUTPUT);
  digitalWrite(PinRelays[relaynum],state);
  return;
}



 
void setup()
{
  Serial.begin(115200);
  WiFi.onEvent(WiFiEvent);

 if( ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE))
    {
      Serial.println(ETH.localIP());
        Serial.println(ETH.macAddress());
    }

 while ((ETH.localIP().toString()).startsWith("0.0") ) {}; /// Waiting IP 
 
 location+="http://"+(ETH.localIP().toString())+ ":" + "8898";
 group=getUID();

class MppTokens relays(Srelays, ',');

for (int i = 0;; i++) {
    String r = relays.next();
    if (r.length() == 0 || i==9)  break;
    JsonRelays[i]=makeJsonString(i); /// .c_str()+ "_"+i; // .toString().c_str();
    PinRelays[i]=r.toInt();
 }

  Udp.begin(localPort);
  Serial.println("UDP Service started");
  server.begin(localPort); // Server starts listening on port number 8898
  Serial.printf("Server started to listen at port %d\n",localPort);

}

void loop()
{
String InputString;
unsigned long now = millis();



  if (eth_connected)
  {
 CheckUdpDiscovery();
  

     WiFiClient client = server.available(); 
 
    if (client) // If current customer is available
    {
        Serial.println("[Client connected]");
        Serial.printf("Remore client IP:%s\n",client.remoteIP().toString().c_str());
       Subscriber[Subscriber_num]=client.remoteIP().toString().c_str();
          Subscriber_num++ ;
      if(Subscriber_num>=3) Subscriber_num=0;
             Serial.println("New Subscriber IP stored: "+Subscriber[Subscriber_num]);
              Serial.printf("Num of Subscriber: %d\n",Subscriber_num);
        
        if(Udp.parsePacket())  handleIncomingUdp(Udp);
        
        while (client.connected()) // If the client is connected
        {
      
          if (client.available()) // If there is readable data /// if or while ??
            {
               char c = client.read(); // Read a byte
               if (c != '\n' && c != '\r') InputString += c;

               if (c == '\n' && InputString.startsWith("GET / HTTP/1.1") ) { // Reply to first device discovery
               Serial.println("Answering GET Response:"+InputString ); 
       client.println("HTTP/1.1 200 OK");
       client.println("Content-Type: application/json;charset=utf-8");
       client.println("Server: MppEsp32");
       client.println("Connection: close");
       client.println();
        Serial.println("JSON String for HTTP REPLY:"+makeJsonArray());
       client.println(makeJsonArray());
       client.println("\r\n\r\n");
   //    UpdateStatus(client);
          break;
            } 
               if (c == '\n' && InputString.startsWith("GET /state") ) { // Reply to first device discovery
               Serial.println("Answering GET Response:"+InputString ); 
             unsigned int relnum= ParseGet(InputString)-'0';
                bool staterel=CheckRelayState(relnum);
       client.println("HTTP/1.1 200 OK");
       client.println("Content-Type: application/json;charset=utf-8");
       client.println("Server: MppEsp32");
       client.println("Connection: close");
       client.println();
        Serial.println("JSON String for HTTP REPLY:");
        Serial.println(makeJsonString(relnum));
       client.println(makeJsonString(relnum));
       client.println("\r\n\r\n");
          break;
            } 
          
           if (c == '\n' && InputString.startsWith("GET /survey") ) { // Reply to first device discovery
               Serial.println("Answering GET survey Response:"+InputString ); 
      client.println("HTTP/1.1 200 OK");
       client.println("Content-Type: application/json;charset=utf-8");
       client.println("Server: MppEsp32");
       client.println("Connection: close");
  //       String network = "[{\"ssid\":\"Ethernet\",\"bssid\":\"\",\"channel\":\"\",\"rssi\":\"\",\"auth\":\"OK\"}]";
       String network = "[]";
       client.println(network);
       client.println("\r\n\r\n");
  /*      Udp.beginPacket(client.remoteIP().toString().c_str(), localPort);
        int result = Udp.write((const uint8_t *)network.c_str(),network.length());
        Udp.endPacket();*/
          break;
           }
          if (c == '\n' && InputString.startsWith("PUT /state") ) {
                      Serial.println("Answering PUT Response:"+InputString );
                       unsigned int relnum= ParsePut(InputString)-'0';
                        
               client.println("HTTP/1.1 200 OK");     
               client.println("Content-Type: application/json;charset=utf-8");
               client.println("Server: MppEsp32");
               client.println("Connection: close");
                client.println();
                  if(InputString.indexOf("true")>0) SetRelayState(relnum,true);
                  if(InputString.indexOf("false")>0) SetRelayState(relnum,false);
 // Serial.printf("Device state:%s\n",device_state ? "true" : "false");               
 Serial.println("JSON String for HTTP REPLY:");
Serial.println(makeJsonString(relnum));

       client.println(makeJsonString(relnum));
         client.println("\r\n\r\n");
          break;
            }
             if (c == '\n' && InputString.startsWith("PUT /subscribe") ) {
                      Serial.println("Answering PUT Response:"+InputString );
            client.println("HTTP/1.1 200 OK");     
               client.println("Content-Type: application/json;charset=utf-8");
                client.println("Server: MppEsp32");
                  client.println("Connection: close");
                    client.println();
                      client.println("\r\n\r\n");
                    break;
            }
            if (c == '\n' && InputString.startsWith("GET /favicon.ico HTTP/1.1")   ) {  // 
              Serial.println("Answering GET query:"+InputString );
              client.println("HTTP/1.1 200 OK");     
               client.println("Content-Type: application/json;charset=utf-8");
                client.println("Server: MppEsp32");
                  client.println("Connection: close");
                    client.println();
                      client.println("\r\n\r\n");
                      client.stop(); //End the current connection
                        Serial.println("[Client disconnected]" );
            }
          
        }

      }
 client.stop(); //End the current connection
 Serial.println("[Client disconnected]" );
 Serial.println("Line HTTP:"+InputString );
    }
  } else {
  next = now + checkin;
  if (now > next)              {
    Serial.println("Ethernet disconnected , check cable!");
  }
  }
  
  
}
