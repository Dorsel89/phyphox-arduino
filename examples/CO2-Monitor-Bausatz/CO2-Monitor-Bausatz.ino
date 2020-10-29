#include <phyphoxBle.h>
#include "SparkFun_SCD30_Arduino_Library.h" 
#include <FS.h>
#include <SPIFFS.h>

bool FORMATFLASH = false;

SCD30 airSensor;
File fsUploadFile;                                    // a File variable to temporarily store the received file

int datasetNumber = 0;
int lineNumber = 0;

float topThreshold = 1000;
float bottomThreshold = 800;
//store data every 12s over 24h -> maxDatasets = 24*60*60/2 = 7200
int maxDatasets=43200;

const int measuredDataLength = 4;
float measuredData[measuredDataLength];  //co2,temperature,humidity,seconds since uptime, number of dataset

float averageMeasuredData[measuredDataLength];
int averageOver = 6; //6*2s rate = store data every 12s
int averageCounter=0;

void receivedConfig();

int oldDataTransmissionOffset = -1;
int oldDataTransmissionSet = -1;

int pinGreen = 26;
int pinRed = 33;
int pinBlue = 25;
bool RED = 1;
bool GREEN = 0;
bool BLUE = 1;

void setup() {
  PhyphoxBLE::start("CO2 Monitor");                 //Start the BLE server
  PhyphoxBLE::configHandler=&receivedConfig;
  Serial.begin(115200);

  // initialize rgb-led
  pinMode(pinGreen, OUTPUT);
  pinMode(pinRed, OUTPUT);
  pinMode(pinBlue, OUTPUT);
  digitalWrite(pinGreen, GREEN);
  digitalWrite(pinRed, RED);
  digitalWrite(pinBlue, BLUE);
  
  Wire.begin();
  if (airSensor.begin(Wire, false) == false)
  {
    Serial.println("Air sensor not detected. Please check wiring. Freezing...");
    while (1);
  }

  delay(200);
  Serial.println("init spiffs");
  initStorage();               // Start the SPIFFS and list all contents

}

void loop() {

  if (airSensor.dataAvailable())
  {
    measuredData[0]=airSensor.getCO2();
    measuredData[1]=airSensor.getTemperature();
    measuredData[2]=airSensor.getHumidity();
    measuredData[3] = millis()/1000;

    echoDataset("Measured", measuredData);

    PhyphoxBLE::write(measuredData[0],measuredData[1],measuredData[2],measuredData[3]);     //Send value to phyphox  

    if(averageCounter < averageOver){
      averageMeasuredData[0]+=measuredData[0]/averageOver;
      averageMeasuredData[1]+=measuredData[1]/averageOver;
      averageMeasuredData[2]+=measuredData[2]/averageOver;
      averageMeasuredData[3]+=measuredData[3]/averageOver;
      averageCounter+=1;
    }else{
      storeMeasuredData(averageMeasuredData);
      averageMeasuredData[0]=0;
      averageMeasuredData[1]=0;
      averageMeasuredData[2]=0;
      averageMeasuredData[3]=0;
      averageCounter=0;      
    }
    
    
    updateLED(measuredData[0]);
    delay(10);
  }

  if (oldDataTransmissionOffset >= 0) {
    if (transferOldData(oldDataTransmissionSet, oldDataTransmissionOffset))
      oldDataTransmissionOffset++;
    else {
      oldDataTransmissionOffset = -1;
      Serial.println("Transfer of old date completed.");
    }
    delay(10);
  }
}

void echoDataset(String note, float * data) {
  Serial.print(note);
  Serial.print(" => CO2: ");
  Serial.print(data[0]);
  Serial.print(", Temperature: ");
  Serial.print(data[1]);
  Serial.print(", Humidity: ");
  Serial.print(data[2]);
  Serial.print(", Timestamp: ");
  Serial.println(data[3]);
}

void initStorage() { // Start the SPIFFS and list all contents
  if (!SPIFFS.begin(true)) {
      Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  if(FORMATFLASH){
   bool formatted = SPIFFS.format();
   if ( formatted ) {
    Serial.println("SPIFFS formatted successfully");
   } else {
    Serial.println("Error formatting");
   }
  }

  int totalBytes = SPIFFS.totalBytes();
  int usedBytes = SPIFFS.usedBytes();
  Serial.print("total Bytes ");
  Serial.println(totalBytes);
  Serial.print("total Bytes ");
  Serial.println(usedBytes); 

  File file = SPIFFS.open("/set"+String(datasetNumber)+".txt", FILE_WRITE);
  file.close();

}

void printSetData(float _setNumber){
  File file = SPIFFS.open("/set"+String(_setNumber)+".txt", "r");
  char buffer[4*measuredDataLength];
  while (file.available()) {
    int l = file.readBytes(buffer, 4*measuredDataLength);
    float bufferArray[measuredDataLength];
    memcpy(&bufferArray[0],&buffer[0],4*measuredDataLength);
    Serial.print(bufferArray[0]);
    Serial.print(" ");
    Serial.print(bufferArray[1]);
    Serial.print(" ");      
    Serial.print(bufferArray[2]);
    Serial.print(" ");      
    Serial.println(bufferArray[3]);
    }
}
void receivedConfig(){

    /*
    byte    information
    0       bool transfer Data
    1       bool calibrate
     */

    uint8_t readArray[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    PhyphoxBLE::read(&readArray[0],20);
    if(readArray[0]==1){
        // just send current data
        Serial.println("Resending of old data requested.");
        oldDataTransmissionOffset = 0;
        oldDataTransmissionSet = datasetNumber;
    }
    if(readArray[1]==1){
      //CALIBRATION
      Serial.print("Calibration with fresh air ");
      airSensor.setAutoSelfCalibration(false);
      Serial.println(airSensor.setForcedRecalibrationFactor(400));
      }
}

bool transferOldData(int setNumber, int offset){
    File file = SPIFFS.open("/set"+String(setNumber)+".txt", "r");
    file.seek(offset*4*measuredDataLength, SeekSet);
    char buffer[4*measuredDataLength];
    if (!file.available()) {
      file.close();
      return false;
    }
      
    int l = file.readBytes(buffer, 4*measuredDataLength);
    file.close();
    
    float bufferArray[measuredDataLength];
    memcpy(&bufferArray[0],&buffer[0],4*measuredDataLength);

    echoDataset("Old", bufferArray);
    
    PhyphoxBLE::write(bufferArray[0],bufferArray[1],bufferArray[2],bufferArray[3]);     //Send value to phyphox
    
    return true;
}

void storeMeasuredData(float dataArray[4]){
  byte byteArray[4*measuredDataLength];
  memcpy(&byteArray[0],&dataArray[0],4*measuredDataLength);
  File file = SPIFFS.open("/set"+String(datasetNumber)+".txt", "a");  
  file.seek(lineNumber*4*measuredDataLength, SeekSet);    
  file.write(byteArray, 4*measuredDataLength);
  file.close();
  
  if(lineNumber<(maxDatasets-1)){
      lineNumber+=1;
    }else{
      lineNumber=0;
  }
}

void updateLED(float co2value){
  if(co2value>topThreshold && GREEN == false){
    GREEN=true;
    RED=false;
    digitalWrite(pinGreen, GREEN);
    digitalWrite(pinRed, RED);
  }
  if(co2value<bottomThreshold && RED == false){
    GREEN=false;
    RED=true;
    digitalWrite(pinGreen, GREEN);
    digitalWrite(pinRed, RED);
  }  
    
}
  