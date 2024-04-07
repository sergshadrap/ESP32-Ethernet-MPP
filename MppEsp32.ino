#include <Arduino.h>
#include <ETH.h>
#include <WiFiUdp.h> 
#include <EEPROM.h>

#define UDP_TX_PACKET_MAX_SIZE 2048
/*
   * ETH_CLOCK_GPIO0_IN   - default: external clock from crystal oscillator
   * ETH_CLOCK_GPIO0_OUT  - 50MHz clock from internal APLL output on GPIO0 - possibly an inverter is needed for LAN8720
   * ETH_CLOCK_GPIO16_OUT - 50MHz clock from internal APLL output on GPIO16 - possibly an inverter is needed for LAN8720
   * ETH_CLOCK_GPIO17_OUT - 50MHz clock from internal APLL inverted output on GPIO17 - tested with LAN8720
*/
#undef ETH_CLK_MODE
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
const char* DeviceVersion = "Mpp32Relays 1.2.1"; // Keep all states and tunes in EEPROM , and restore after power shutdown, support renaming
                                                 // UDP discovery , UDP notifications

    
static bool eth_connected = false;
WiFiServer server; // Declare server objects
WiFiServer webserver(80); // Declare web server 
WiFiUDP Udp;

String udn="",location,group;
String JSONReply="";
String BroadcastIP="239.255.255.250";
String Subscriber[4];
int Subscriber_num=0;

unsigned int localPort = 8898;      // local port to listen on
// unsigned int BroadcastPort = 1900;
boolean device_state=false;
String Srelays="";
String JsonRelays[10];
String DeviceName;
unsigned int PinRelays[10];
unsigned long lastnotify;
unsigned long next = millis();
static String UID;

#define MaxProps 2048
#define MppMarkerLength 14
#define MppPropertiesLength MaxProps - MppMarkerLength
static const char MppMarker[MppMarkerLength] = "MppProperties";
static char propertiesString[MppPropertiesLength];


static bool writeProperties(String target) {
  if (target.length() < MppPropertiesLength) {
    for (unsigned i = 0; i < MppPropertiesLength && i < target.length() + 1;
        i++)
      EEPROM.put(i + MppMarkerLength, target.charAt(i));
    EEPROM.commit();
    Serial.printf("Saved properties (%d bytes).\n", target.length());
    return true;
  } else {
    Serial.println("Properties do not fit in reserved EEPROM space.");
    return false;
  }
}

void parseProperties(String newString, unsigned i, unsigned j, unsigned k)  {
  JsonRelays[k]=newString.substring(j,i+1);
      Serial.print("Properties loaded:"+ JsonRelays[k]);
}

void beginProperties() {
  EEPROM.begin(MaxProps);
  for (int i = 0; i < MppMarkerLength; i++) {
    if (MppMarker[i] != EEPROM.read(i)) {
      Serial.println("EEPROM initializing...");
      EEPROM.put(0, MppMarker); 
      if(writeProperties("")) {
           Serial.println(" EEPROM initialized");
            break;
    }
    else { break; Serial.print("Error EEPROM initialization"); return;} 
   }
  }

  for (unsigned i = 0; i < MppPropertiesLength; i++) {
    propertiesString[i] = EEPROM.read(i + MppMarkerLength);
    if (propertiesString[i] == 0)
      break;
  }
    unsigned k =0,j=0;  
    
    for (unsigned i = 0; i < MppPropertiesLength; i++) {
     if (propertiesString[i] == '{') j=i; // mark the very beginning of JSON {
     if (propertiesString[i] == '}' && propertiesString[i+1] == ';')  {
   String pin ="";
        pin.concat(propertiesString[i+2]);
        if(propertiesString[i+3]!=';') pin.concat(propertiesString[i+3]); // if the relay pin consist of 1 or 2 digits
          PinRelays[k]=pin.toInt();
         parseProperties(propertiesString,i,j,k);
         Serial.printf(" for relay's pin:%d\n",PinRelays[k]);
         SetInitRelayState(JsonRelays[k],PinRelays[k]);
      k++;
     }
  }
}


void SetInitRelayState( String JsonProperties, unsigned RelayPin) {
  String Rstate=JsonProperties.substring(JsonProperties.indexOf("state")+8,JsonProperties.indexOf("group")-3);
  if(strcmp(Rstate.c_str(),"on")==0)  { pinMode(RelayPin,OUTPUT);
                                          digitalWrite(RelayPin,true);}
  if(strcmp(Rstate.c_str(),"off")==0) { pinMode(RelayPin,OUTPUT);
                                          digitalWrite(RelayPin,false);}
//  Serial.printf("Relay pin %d will be set to %s Json:%s comparison=%d\n", RelayPin,Rstate.c_str(),JsonProperties.c_str(),strcmp(Rstate.c_str(),"on"));
//  delay(100);
//  Serial.printf("Checking the %d relay state is %s .\n",RelayPin,digitalRead(RelayPin) ? "true" : "false");
  Rstate="";
}

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

void UpdateProperties( String Properties) {
  class MppTokens relays(Properties, ',');

String Prop="";
for (int i = 0;; i++) {
    String r = relays.next();
    if (r.length() == 0 || i==9)  break;
    JsonRelays[i]=makeJsonString(i); // create JSON for relay Pin and store in JsonRelays[]
    PinRelays[i]=r.toInt();           // store relay Pin []
   Prop +=JsonRelays[i]+";"+r+";";
   Serial.printf("Properties updated: PinRelay[%d]=%d String=%s\n",i,PinRelays[i],JsonRelays[i].c_str());
    }
if(Prop.length()>0) writeProperties(Prop); // save current Json & pin in EEPROM
}

void UpdateCurrentProperties(void) {   // if properties already exist - refresh them and store in EEPROM
  String Prop="";
  for (int i = 0;i<=9; i++) {
    Serial.printf("Upading Properties PinRelay %d , JsonRelays[%d]: %s\n",PinRelays[i],i, JsonRelays[i].c_str());
    if(PinRelays[i]==0) break;
    JsonRelays[i]=makeJsonString(i);
    Prop +=JsonRelays[i]+";"+String(PinRelays[i])+";";
    Serial.printf("Properties updated: PinRelay[%d]=%d String=%s\n",i,PinRelays[i],JsonRelays[i].c_str());
    }
   if(Prop.length()>10) writeProperties(Prop); // save current Json & pin in EEPROM 
}


bool addSubscriber(String ip) {
  unsigned count =3; // restricted Subscriber size
  for (int i = 0; i <= count; i++) {
    if ((strcmp(Subscriber[i].c_str(), ip.c_str()) == 0) || Subscriber[i]=="")  {
      if(Subscriber[i]) { 
        Subscriber[i]= ip.c_str();  
         Serial.printf("%s subscribed as Subscriber[%d] \n", Subscriber[i].c_str(),i);
          return true;
        }
    } else  {
      if(i==3)  {
        count=0;
         Subscriber[i]= ip.c_str();  
         Serial.printf("%s subscribed as Subscriber[%d] \n", Subscriber[i].c_str(),i);
          return true;
      }
    }
  }
  return false;
}


void notifySubscribers(void) {
    unsigned long now = millis();
        if (now > lastnotify+600000) {  // notifying every 10 min
          
          for (int i = 0; i <= 3; i++) {  //3 -is max subscribers
             if(Subscriber[i]==0) break;
             for (int j = 0;; j++) {
              String message = "";
              if (JsonRelays[j].length() == 0)  break;
                message+=JsonRelays[j].c_str();
                Serial.printf("Notifying: %s  ... with %s\n",Subscriber[i].c_str(),message.c_str());
                  Udp.beginPacket(Subscriber[i].c_str(), localPort);
      //          int result = Udp.write((const uint8_t *)JsonQuery.c_str(),JsonQuery.length());
                  Udp.write((const uint8_t *)message.c_str(),message.length());
                  Udp.endPacket();
          Serial.println("Notifications sent.");
             }
         }
       lastnotify=millis();
     }      
  return;
}

void SendBroadcastUDP()  {

  if (makeJsonArray() == 0) return;  
 
  Udp.beginPacket(BroadcastIP.c_str(), localPort);
//       int result = Udp.write((const uint8_t *)makeJsonString(i).c_str(),makeJsonString(i).length());
        Udp.write((const uint8_t *)makeJsonArray().c_str(),makeJsonArray().length());
        Udp.endPacket();
        return;
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
        SendBroadcastUDP(); // Broadcast UDP
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
//        int result = Udp.write((const uint8_t *)JSONR.c_str(),JSONR.length());
          Udp.write((const uint8_t *)JSONR.c_str(),JSONR.length());
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
  String Rname=JsonRelays[Rnumber].substring(JsonRelays[Rnumber].indexOf("name")+7,JsonRelays[Rnumber].indexOf("udn")-3);
  if(Rname=="") Rname=getDefaultUDN()+"_"+Rnumber;
  if(DeviceName!="") Rname=DeviceName;
//  Serial.println("Rname: "+Rname);
//  Serial.println("DeviceName: "+DeviceName);
//  Serial.println("Json: "+Rname);
  JSONReply = "{\"location\":\"" +location+"\",\"state\":\"" +( device_state ? "on" : "off") + "\",\"group\":\""+ group +"\",\"name\":\""+Rname+"\",\"udn\":\"" + getDefaultUDN() +"_"+Rnumber+"\"}";
//  Serial.println("New Json Str: "+JSONReply);
  JsonRelays[Rnumber]=JSONReply ;
  DeviceName="";
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

bool handleIncomingUdp(WiFiUDP &Udp) {
  char incoming[16]; // only "discover" is accepted
// receive incoming UDP packets
// Serial.printf("Received from %s, port %d\n", Udp.remoteIP().toString().c_str(), Udp.remotePort());
  int len = Udp.read(incoming, sizeof(incoming) - 1);
  if (len > 0) {
    incoming[len] = 0;
   Serial.printf("UDP packet contents: %s\n", incoming);
    if (String(incoming).startsWith("discover"))
      sendDiscoveryResponse(Udp.remoteIP(), Udp.remotePort());
    else return false;  
  }
  return true;
}


char ParseGet (String InputString)  {
  if(InputString.indexOf(getDefaultUDN())!=-1) {
//    Serial.printf("String: %s  compare:%d  symbol:%c \n", InputString.c_str(), InputString.lastIndexOf(" HTTP/1.1"),InputString.charAt(InputString.lastIndexOf(" HTTP/1.1")-1) );
//    Serial.printf("Comparison to : %s  number of similar: %d\n",getDefaultUDN().c_str(),InputString.indexOf(getDefaultUDN()));
  return InputString.charAt(InputString.lastIndexOf(" HTTP/1.1")-1);
  } else {
  Serial.println("Non autorised device!");  
  return 11; // return something that reach the limits
  }
 }

 char ParsePut (String InputString)  {
  if(InputString.indexOf(getDefaultUDN())) {
  return InputString.charAt(InputString.lastIndexOf("?state=")-1);
  }
  return -1;
 }

bool ParsePutName(String InString)   {  // parse changing name for device
  String nam=getUID()+"_";
  unsigned int relnum=0;
  Serial.println("Nam:"+nam);
  String R=InString.substring(InString.lastIndexOf(nam),InString.lastIndexOf("?name=")-1);
if(R)
   relnum=InString.charAt(InString.lastIndexOf("?name=")-1)-'0';
  else return false;
  
    String Rname=InString.substring(InString.lastIndexOf("?name=")+6,InString.indexOf("HTTP/1.1")-1);
    String JRname=JsonRelays[relnum].substring(JsonRelays[relnum].indexOf("name")+7,JsonRelays[relnum].indexOf("udn")-3);
    if(Rname.compareTo(JRname)!=0) {
      DeviceName=Rname;
      makeJsonString(relnum);
    }
 //   Serial.print("String:"+JsonRelays[relnum]+" name:"+JRname +" newname:"+Rname);
    return true;
}

boolean CheckRelayState(int relaynum) {
  return digitalRead(PinRelays[relaynum]);
}

void SetRelayState(unsigned relaynum, boolean state)  {
  pinMode(PinRelays[relaynum],OUTPUT);
  digitalWrite(PinRelays[relaynum],state);
//  Serial.printf("Relay %d is in %s state relaynum:%d\n",PinRelays[relaynum],state ? "on" : "off", relaynum);
//  Serial.printf("Relay [0]=%d, Relay[1]=%d,Relay[2]=%d, Relay[3]=%d\n",PinRelays[0],PinRelays[1],PinRelays[2],PinRelays[3]);
  return;
}



 
void setup()
{
  Serial.begin(115200);
  WiFi.onEvent(WiFiEvent);
  beginProperties();

 if( ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE))
    {
      Serial.println(ETH.localIP());
        Serial.println(ETH.macAddress());
    }

 while ((ETH.localIP().toString()).startsWith("0.0") ) {}; /// Waiting IP 
 
 location+="http://"+(ETH.localIP().toString())+ ":" + "8898";
 group=getUID();

if(Srelays=="") Srelays="12,14";  // make default relay pin assigment
bool flagRelay=true;
    class MppTokens relays(Srelays, ',');
    for (int i = 0;; i++) {
    String r = relays.next();
    if(PinRelays[0]==0) flagRelay=false;
    if (r.length() == 0 || i==9)  break;
    }
 if(!flagRelay) {
   Serial.print("Properties are not properly set , Updating to default...");
  UpdateProperties(Srelays);
 } else Serial.print("Stored properties are loaded!");
 
  Udp.begin(localPort);
  Serial.println("UDP Service started");
  server.begin(localPort); // Server starts listening on port number 8898
  Serial.printf("Server started to listen at port %d\n",localPort);

  webserver.begin(80); 
  Serial.println("Web Server started on port 80.");
}

void loop()
{
String InputString;
unsigned long now = millis();



  if (eth_connected)
  {
 // CheckUdpDiscovery();
  handleIncomingUdp(Udp);

     WiFiClient client = server.available(); 
      WiFiClient webclient = webserver.available(); 
      if(webclient) 
      {
         Serial.println("new web client");

    boolean currentLineIsBlank = true;
    while (webclient.connected()) {
      if (webclient.available()) {
        char c = webclient.read();
        if (c != '\n' && c != '\r') InputString += c;
             if (c == '\n' && currentLineIsBlank) {
            webclient.println("HTTP/1.1 200 OK");
            webclient.println("Content-Type: text/html");
            webclient.println("Connection: close");  // the connection will be closed after completion of the response
            webclient.println();
            webclient.println("<!DOCTYPE HTML>");
             webclient.println("<html>");
              webclient.println("<body><H1>Mpp Ethernet ESP32 multirelay</H1>");
                webclient.println("<form method=GET style='display:inline' > Enter relays pin number comma separated: <input type=text name=Rstring SIZE=20 ><input type=submit value=Submit_changes></form>");
                    webclient.println("<br /> <br />After you submited the relays pin , please make search for new MppDevice on AM Server again! <br /><br />");
                        webclient.println("&nbsp;<input type=button value='Restart' style='width:150px' onmousedown=location.href='/?RestartESP;'>"); 
                          webclient.println("</html>");
                            
          break;
          }
          if (c == '\n') currentLineIsBlank = true;
              else if (c != '\r') currentLineIsBlank = false;
         }
    }  
    delay(3);
    // close the connection:
    webclient.stop();
    Serial.println("client disconnected");
  Serial.println("Line HTTP:"+InputString );
    Srelays="";
    if(!InputString.startsWith("GET /favicon.ico HTTP/1.1") && InputString.indexOf("Rstring=")!=-1) // filtering   /favicon.ico query
    {
      String rel3=InputString.substring(InputString.indexOf("Rstring=")+8,InputString.lastIndexOf("HTTP/1.1")-1);
      for (int i = 0;i<rel3.length(); i++) {
      if(rel3.charAt(i)=='%' && rel3.charAt(i+1)=='2' && rel3.charAt(i+2)=='C'){ Srelays+=","; i=i+2;}
        else Srelays+=rel3.charAt(i);
//        Serial.printf("Final String:%s\n",Srelays.c_str());
      }
        InputString=""; 
        Serial.println("Submited!"); 
          UpdateProperties(Srelays);
/*        class MppTokens relays(Srelays, ',');

for (int i = 0;; i++) {
    String r = relays.next();
    if (r.length() == 0 || i==9)  break;
    JsonRelays[i]=makeJsonString(i); 
    PinRelays[i]=r.toInt();
 }*/
        }

    if(!InputString.startsWith("GET /favicon.ico HTTP/1.1") && InputString.indexOf("RestartESP")!=-1) {   // filtering   /favicon.ico query
                                                  InputString=""; 
                                                  Serial.println("ESP rebooted"); 
                                                  delay(250);
                                                  esp_restart();
                                                  }
        
  }
 
 
    if (client) // If current customer is available
    {
        Serial.println("[Client connected]");
        Serial.printf("Remore client IP:%s\n",client.remoteIP().toString().c_str());
              addSubscriber(client.remoteIP().toString().c_str());
        
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

if(relnum>=0 && relnum<=9) {
      CheckRelayState(relnum);
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
          else {
            client.println("HTTP/1.1 404 ERROR");
            client.println("Content-Type: application/json;charset=utf-8");
            client.println("Server: MppEsp32");
            client.println("Connection: close");
            client.println(); 
            break;
          }
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
    UpdateCurrentProperties(); // Refresh status of each relays after changing in EEPROM
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
               if (c == '\n' && InputString.startsWith("PUT /name") ) {
                      Serial.println("Answering PUT Response:"+InputString );
                  if(ParsePutName(InputString)) UpdateCurrentProperties();    
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
    if(Subscriber[0]!=0) notifySubscribers(); // if at least  one subscriber exist
  } else {
  next = now + checkin;
  if (now > next)              {
    Serial.println("Ethernet disconnected , check cable!");
  }
  }
  
  
}
