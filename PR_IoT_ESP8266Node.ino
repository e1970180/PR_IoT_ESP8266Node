    // libs for WiFiManager
        #include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
        //needed for library
        #include <DNSServer.h>
        #include <ESP8266WebServer.h>
        #include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
    // --------------

	#include "PR_IoT_Config.h"        //IoT settings-must bebefore PR_IoT.h
	#include <PR_IoT.h>
	
//************* Devices headers  *************************


#include <PR_IoT_Device_2PosRegulator.h>
#include <PR_IoT_Device_BME280.h>
#include <PR_IoT_Device_Light.h>
#include <PR_IoT_Device_Switch.h>
#include <PR_IoT_Device_Relay.h>
#include <PR_IoT_Device_DS18B20.h>
#include <PR_IoT_Device_SI7021.h>


//************* PINs allocation *************************

//#define   FLASH       0    //D1mini=D3 //functions   
//#define     TX          1    //D1mini=TX //functions 
//#define     UART1       2    //D1mini=D4 //functions UART1 output, flashing!?
//#define     RX          3    //D1mini=RX //functions 
#define     RELAY       4    //D1mini=D2 //functions   Only use is as a GPIO. GREEN         used heater as output relay 
//#define     SWITCH      5    //D1mini=D1 //functions   Only use is as a GPIO. GREEN         used switch as input manual tumbler
#define     ONEWIRE     12   //D1mini=D6 //functions = MISO                     GREEN       used Temp sensor
#define     LEDg        13   //D1mini=D7 //functions = MOSI                     GREEN       used LED as output
//#define     LEDr        14   //D1mini=D5 //functions = CLK                      GREEN       used LED as output
//#define     CS          15   //D1mini=D8 //functions = CS
//#define      16            //D1mini=D0 //functions = WAKE
//#define     ADC            //D1mini=A0 //functions = analog     
//************* end PINs allocation *************************


//************* Global vars *************************
    
PR_IoT_NodeMQTTClass    NodeMQTT("bathroom", "floor_heater");

bool shouldSaveConfig = false;   //flag for saving creditals after WiFiManager             

WiFiClient      ESP8266Client;
PubSubClient    MQTTclient(ESP8266Client);


////***********    Locally attached devices    **********

PR_IoT_Relay            deviceLEDgreen("ledGreen");
PR_IoT_DS18B20          deviceTemp("floortemp");
PR_IoT_2PosRegulator    device2PosRegulator("temp_regulator");


void setup() {

    
//************ Serial 					***************
    Serial.begin(115200);
    
//************ Local HadrWare setup 	***************
       
    deviceLEDgreen.setupHW(LEDg, HIGH);   
    deviceTemp.setupHW(ONEWIRE);                      //
    device2PosRegulator.setupHW(RELAY, HIGH, 1.0, 1.0, 200);    //(pin, bool onValue, float hysteresysL, float hysteresysH, uint16_t freqLimitationMS = 0);
    device2PosRegulator.setTargetValue(24);                     //initial value before MQTT set it

//******************    setup network connections   **************
  	
	String  reasonForConfig;
  
	WiFi.begin();
    do {       
        reasonForConfig = "";
        
		PR_DBGTLN("trying connect WiFi")        
        int wifiConnectionCountdown = 20;             
        while (WiFi.status() != WL_CONNECTED && wifiConnectionCountdown > 0 ) {
            delay(1000);
			PR_DBGT(".")
			wifiConnectionCountdown--;	
            if ( wifiConnectionCountdown == 0 ) {
                PR_DBGTLN("WiFi connection failed")
                reasonForConfig += "WiFi connection failed";   
            }
        }
		     
        if ( NodeMQTT.creditals.restore() ) {
			PR_DBGTLN("MQTT creditals restored OK"); 
            
            if ( WiFi.isConnected() ) {
                PR_DBGTLN("WiFi connected, connecting to MQTT")
                //************ MQTT setup   ***************       
                MQTTclient.setServer(NodeMQTT.creditals.serverIP, NodeMQTT.creditals.port);
                MQTTclient.setCallback(callbackMQTT);
    
                int mqttConnectionCountdown = 5;             
                while ( (mqttConnectionCountdown > 0) && !NodeMQTT.connect(String(ESP.getChipId()).c_str()) ) {
                    delay(1000);
					mqttConnectionCountdown--;	
                    if ( mqttConnectionCountdown == 0 ) {
                        PR_DBGTLN("MQTT connection not established")
                        reasonForConfig += " MQTT server connection failed";   
                    }
                }
            } //if ( WiFi.isConnected() )
        }   
        else {
            PR_DBGTLN("MQTT creditals restored FAIL")
            reasonForConfig += " no MQTT creditals saved"; 
        }
		
        if ( reasonForConfig != "" ) {
			WiFiconnectionWizard(reasonForConfig);
		}
    } while ( reasonForConfig != "");
  

    //WiFi.setAutoReconnect(true);
      
//***********	Devices begin([s])	*****************
		        
        deviceLEDgreen.begin(-1);   
        deviceTemp.begin(10);               //report temp          
        device2PosRegulator.begin(0);

PR_DBGTLN("SETUP FINISHED");
}//setup

void loop() {

//*********** check connections & reconnecting		*****************
	if (WiFi.isConnected()) { 
		NodeMQTT.connect( String(ESP.getChipId()).c_str() );							//re-connect if MQTT connection lost
		MQTTclient.loop();
	} 
	else {		//WiFi connection lost
		NodeMQTT.setOnline(false);
		PR_DBGTLN("WiFi connection lost")
	}
	
	IoTtime.update();

	
//*********** all  Devices.loop()	*****************
    
    deviceLEDgreen.loop();
    deviceTemp.loop();

    //transfer floor temp to regulator and protect against temp sensor faulre
    float t = deviceTemp.getTemp();
    if (t < 5 ) t = +254;                 // if sensor not connected it measure -127C, set t to very high to switch off heating
    device2PosRegulator.setCurrentValue(t);
    
    device2PosRegulator.loop();  
       
	//sysDevice.loop();
    inMsg.newMsgFlag = false; //skip this msg once no device recognized it 
    delay(0);

} //loop


void    WiFiconnectionWizard(String  &reason) {
	
	WiFiManager     wifiManager;
	char			r[255];
	
    PR_DBGT("Strating WiFi manager with reason:")
	PR_DBGVLN(reason)

	reason = "<br/>Reason: " + reason + "<br/>"; 	

	
//wifiManager.resetSettings();
	
    WiFiManagerParameter custom_mqtt_text("<br/>MQTT config: <br/>");
    wifiManager.addParameter(&custom_mqtt_text);
    
    WiFiManagerParameter custom_mqtt_host("MQTTIP", "MQTT server IP", NodeMQTT.creditals.serverIP.toString().c_str(), 16);
    wifiManager.addParameter(&custom_mqtt_host);
    
    WiFiManagerParameter custom_mqtt_port("MQTTport", "MQTT server port", String(NodeMQTT.creditals.port).c_str(), 6);
    wifiManager.addParameter(&custom_mqtt_port);
    
    WiFiManagerParameter custom_mqtt_username("MQTTusername", "MQTT username", NodeMQTT.creditals.username, PR_IoT_MQTT_NAMEPW_MAX_LEN);
    wifiManager.addParameter(&custom_mqtt_username); 
    
    WiFiManagerParameter custom_mqtt_password("MQTTpassword", "MQTT password", NodeMQTT.creditals.password, PR_IoT_MQTT_NAMEPW_MAX_LEN);
    wifiManager.addParameter(&custom_mqtt_password);
    
	strncpy( r, reason.c_str(), 255 );
    WiFiManagerParameter custom_reason_text( r );	//doesn't work if reason.c_str() direct
    wifiManager.addParameter(&custom_reason_text);
	
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    String ssidAP = "ESP" + String(ESP.getChipId());
    wifiManager.startConfigPortal(ssidAP.c_str(), ssidAP.c_str());	//use PW same as SSIDAP
    
	wifiManager.autoConnect();

    if (shouldSaveConfig) {
        NodeMQTT.creditals.serverIP.fromString( custom_mqtt_host.getValue() );
        NodeMQTT.creditals.port = String(custom_mqtt_port.getValue()).toInt();
        strcpy( NodeMQTT.creditals.username, custom_mqtt_username.getValue() );
        strcpy( NodeMQTT.creditals.password, custom_mqtt_password.getValue() );

        NodeMQTT.creditals.save();
    }

}

//callback notifying us of the need to save config
void saveConfigCallback () {
    PR_DBGTLN("Should save config") 
  shouldSaveConfig = true;
}

    void    DEBUG_CreditalsErase() {
		
		char	err[300];
		for (int i = 0 ; i <299; i++) {
			err[i] = 88;
			
		}
        EEPROM.begin(512);
        EEPROM.put(PR_IoT_MQTT_EEPROM_ADDR_CREDITALS, err);
        EEPROM.end();
		PR_DBGTLN("MQTT creditals errased")
	}
