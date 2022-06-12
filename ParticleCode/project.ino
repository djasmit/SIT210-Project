#include <fcntl.h>
#include <cmath>

#include "Adafruit_DHT.h"

//================================================================================================================================
// GLOBAL DATA
//================================================================================================================================

// Based on the DHT example project: https://build.particle.io/libs/Adafruit_DHT/0.0.4/tab/example/dht-test.ino
#define DIGITAL_DEFAULT 0
#define ANALOG_DEFAULT -1   //no default, just random values
#define ANALOG_POWER 255.0 // maximum power to analog pins

#define HOT_PIN A0
#define COLD_PIN A1
#define MST_PIN A2
#define ARID_PIN A3
#define WET_PIN A4

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11		// DHT 11 
#define DHTTYPE DHT11		// DHT 22 (AM2302)
//#define DHTTYPE DHT21		// DHT 21 (AM2301)

#define DELIMITER ';'       //character that breaks up seperate values in strings

#define DELAY 60            //s
#define CHECK_DELAY 500     //ms
#define CHECK_TIMES 10      //10 checks for sensor reading
#define PASS_REQUIRED 7     //must pass 5/10 checks

#define ENVIRO_COUNT 3
#define FIELD_COUNT 5
#define PIN_COUNT 2

#define MOIST_MAX 2000      //this is about the level the sensor reads when submerged in water

#define ERROR_MESSAGE "ERROR"
#define UPDATE_MESSAGE "UPDATE"
#define EMERGENCY_MESSAGE "EMERGENCY"
#define INSTRUCTIONS_MESSAGE "INSTRUCTIONS"

const String DIRECTORY = "/controllerData";
const String FILENAME = "/fieldData.txt";
const String FILEPATH = DIRECTORY + FILENAME;

long timePrev;

bool highSensitivity;
bool hasInstructions;

#define DHTPIN D2   //temp/humid sensor
DHT dht(DHTPIN, DHTTYPE);

//different states each field can be in
enum Status { INIT, SAFE, UNSAFE, DANGER };
const char *Statuses[] = { "INIT", "SAFE", "UNSAFE", "DANGER" };

//different environments
enum EnviroType { TEMP, HUMID, MOIST };
const char *EnviroTypes[] = { "TEMPERATURE", "HUMIDITY", "MOISTURE" };

//environment structure
struct enviroData {
  EnviroType type;
  Status status;
  float fields[FIELD_COUNT];
  int pins[PIN_COUNT];
  float lastValue;
  float minValue, maxValue;
  float defaultValue;
  
} enviroTemp, enviroHum, enviroMst;

//individual environments
enviroData environments[ENVIRO_COUNT];
float tempValues[FIELD_COUNT];
float humValues[FIELD_COUNT];
float mstValues[FIELD_COUNT];

//================================================================================================================================
// WORKING METHODS
//================================================================================================================================

//updates the status for a given data type
void updateStatus(enviroData &environment, float value) {
    EnviroType enviroType = environment.type;
    String typeString = EnviroTypes[enviroType];
    
    Status currentStatus;
    Status lastStatus = environment.status;
    float unsafeMin = environment.fields[0];
    float safeMin = environment.fields[1];
    float safeMax = environment.fields[3];
    float unsafeMax = environment.fields[4];
	
	//safe range is within safe min/max, unsafe range is within unsafe min/max, beyond that is danger zone
	if ((value >= safeMin) && (value <= safeMax)) { currentStatus = SAFE; }
	else if ((value >= unsafeMin) && (value <= unsafeMax)) { currentStatus = UNSAFE; }
	else { currentStatus = DANGER; }    
	
	String info = ": Reading: " + String(value) + ", unsafeMin: " + unsafeMin + ", safeMin: " + String(safeMin) + ", safeMax: " + String(safeMax) +  ", unsafeMax: " + String(safeMax);
    //Particle.publish(String(EnviroTypes[enviroType]), String(info), PRIVATE);
    
	//publish status when it's updated
	if ((currentStatus != lastStatus) && (currentStatus != DANGER)) {
	    String statusString = typeString + " status: " + Statuses[currentStatus] + info;
        Particle.publish(UPDATE_MESSAGE, statusString, PRIVATE);
        environment.status = currentStatus;
	}
	
	//always publish emergency statuses
	else if (currentStatus == DANGER) {
	    String statusString = typeString + " status: " + Statuses[currentStatus] + info;
        Particle.publish(EMERGENCY_MESSAGE, statusString, PRIVATE);
        environment.status = currentStatus;	    
	}
}

//ses the power ouput for the pins of a specified envrironment based on the value
void powerPin(enviroData &environment, float value) {
    int pin;
    int lowPin = environment.pins[0];
    int highPin = environment.pins[1];
    float powerLevel;
    float targetMin, targetMax, min, max;
    
    //turn both pins off
    analogWrite(highPin, 0);
    analogWrite(lowPin, 0);
    
    //high sensitive environment is kept at ideal conditions
    if (highSensitivity == true) {
        targetMin = environment.fields[2];
        targetMax = targetMin;
        min = environment.fields[1];
        max = environment.fields[3];
    }
    
    //low sensitive environment is kept in safe range
    else {
        targetMin = environment.fields[1];
        targetMax = environment.fields[3];
        min = environment.fields[0];
        max = environment.fields[4];
    }
    
    //above ideal range, so select high pin and calculate
    if (value > targetMax) {
        pin = highPin;
        float heatRange = max - targetMax;
        float heatPosition = value - targetMax; //will never quite reach 0 as must be > ieal temp
        float heatPercentRange = heatPosition / heatRange;
        powerLevel = ANALOG_POWER * heatPercentRange;
    }
    
    //below ideal range, so select low pin and calculate
    else if (value < targetMin) {
        pin = lowPin;
        float heatRange = targetMin - min;
        float heatPosition = targetMin - value; //will never quite reach 0 as must be > ieal temp
        float heatPercentRange = heatPosition / heatRange;
        powerLevel = ANALOG_POWER * heatPercentRange;
    }
    
    //at ideal range
    else { powerLevel = 0; }
    
    //analog range: 0 - 255, so bring down anything exceeding range and output
    if (powerLevel > ANALOG_POWER) { powerLevel = ANALOG_POWER; }
    analogWrite(pin, powerLevel);
    
    //String stats = "Value: " + String(value) + "," + String(min) + "," + String(targetMin) + "," + String(targetMax) + "," + String(max);
    //Particle.publish(stats, String(powerLevel), PRIVATE);
}

//ensures that a reading is valid
float verifyReading(enviroData &environment, float &value) {
    String test = String(environment.type) + ": " + String(value) + ", " + environment.lastValue;
    //Particle.publish("Verifying Value", test, PRIVATE);
    
    float threshold = 2;
    float lastValue = environment.lastValue;
    float difference;
    String errorMessage;
    
    //assume data is good, then set to false if bad conditions are met
    bool goodData = true;
    difference = abs(value - lastValue);
    if (difference > threshold) { goodData = false; }
    if (isnan(value) || value == environment.defaultValue) { goodData = false; }
    
    //data is similar to last reading and not null, so assume it's good data
    if (goodData == true) { return true; }
    
    //value is outside of expected range, so we need more readings to verify
    int checks = CHECK_TIMES;
    float values[checks];
    String results;
    
    //take multiple readings in rapid succession and add them into an array
    float newValue;
    long checkLast = Time.now();
    for (int i = 0; i < checks; i ++) {
        delay(CHECK_DELAY);
        EnviroType type = environment.type;
        if (type == TEMP) { newValue = dht.getTempCelcius(); }
        else if (type == HUMID) { newValue = dht.getHumidity(); }
        else if (type == MOIST) { newValue = getMoistPerc(environment.pins[0]); }
        else { 
            errorMessage = "Invalid Environment: " + String(type);
            Particle.publish(ERROR_MESSAGE, errorMessage, PRIVATE);
            return false; 
        }
        
        //add value to array and add it to string for display
        values[i] = newValue;
        results += String(values[i]) + ";";
    }
    
    //display all results
    //Particle.publish("Results: ", results, PRIVATE);
    
    //iterate through three cycles to check if values wihtin range of the first three values appear at least 7 times
    int required = 7;
    int cycles = checks - required;

    for (int i = 0; i < cycles; i++) {
        int count = 0;
        
        //start from the first index after i, if its in range of i then increment count, then go to next index
        for (int j = i + 1; j < checks; j++) {
            difference = abs(values[j] - values[i]);
            if (difference <= threshold) { count ++; }
        }
        
        //values within range of our reading have appeared an acceptable amounn of times, so sensor is probably okay
        if (count >= required) {
            
            //display resulting value
            //String message = String(i) + ", " + String(values[i]);
            //Particle.publish("RESULT", message, PRIVATE);
            
            //if most values are empty or default then the sensor isn't working
            if (isnan(values[i]) || (values[i] == environment.defaultValue)) { 
                errorMessage = String(EnviroTypes[environment.type]) + " sensor not operating: " + results;
            	Particle.publish(ERROR_MESSAGE, errorMessage, PRIVATE);            
                return false; 
            }
            
            //results seem good, so assume sensor is working
            //String message = String(i) + ", " + String(values[i]);
            //Particle.publish(UPDATE_MESSAGE, message, PRIVATE);  
            return values[i]; 
        }
    }

    //most readings are not similar to first, so sensor probably isn't working
    errorMessage = String(EnviroTypes[environment.type]) + " sensor mulfunction: " + results;
	Particle.publish(ERROR_MESSAGE, errorMessage, PRIVATE);
    return false;
}

//method to handle operations on receiving subscription event
void myHandler(const char *event, const char *data) {
    String dataString = String(data);
    if (interpretData(dataString) == true) { hasInstructions = true; }
    else { Particle.publish(ERROR_MESSAGE, "Corrupted Instructions", PRIVATE); }
}

//verifies and responds to a reading given for a specified environment
void handleReading(enviroData &environment, float value) {
	if (verifyReading(environment, value) == false) { return; }
	
	//have reading, now update the status
	environment.lastValue = value;
    updateStatus(environment, value);
    powerPin(environment, value);    
}

//converts a moisture analog reading into a percantage
float getMoistPerc(int pint) {
    float raw = analogRead(enviroMst.pins[0]);
    float percentWater = (raw / MOIST_MAX) * 100;
    if (percentWater > 100) { percentWater = 100; }
    
    return percentWater;
}

//gets sensor to read temperature and updates status
void readTemp() {
	float temp = dht.getTempCelcius(); //reead temp sensor as celcius
	handleReading(enviroTemp, temp);
}

//gets sensor to read temperature and updates status
void readHum() {
	float hum = dht.getHumidity();
	handleReading(enviroHum, hum);
}

void readMoist() {
    float moist = getMoistPerc(enviroMst.pins[0]); //get moisture level as percentage
	if (verifyReading(enviroMst, moist) == false) { return; }
	
	//have reading, now update the status
	enviroMst.lastValue = moist;
    updateStatus(enviroMst, moist);    
}

//takes a string as an input and attempts to convert it into environment paramaters
bool interpretData(String dataString) {
    
    //first make sure there's a delimiter at the end of the string to act as a break
    if (dataString[dataString.length() - 1] != DELIMITER) { dataString += DELIMITER; }
    
    int arraySize = ENVIRO_COUNT * FIELD_COUNT;
    String fieldsArray[arraySize];
    float fieldsMatrix[ENVIRO_COUNT][FIELD_COUNT];
    
    char sensitivityChar = dataString[0]; //first element is our sensitivity bool
    bool setSensitivity;
    
    //make sure our sensitivity value is a boolean
    if (sensitivityChar == '0') { setSensitivity = false; }
    else if (sensitivityChar == '1') { setSensitivity = true;}
    else { return false; }

    //valid sensitivity switch, so collect field data
    int fieldsIndex = 2; //1 for switch, 1 for delimiter
    String fieldsString = dataString.substring(fieldsIndex);
    
    //cycle through all characters in string
    int currentIndex = 0;
    for (int i = 0; i < arraySize; i++ ) {
        if (currentIndex >= fieldsString.length()) { return false; }
        
        //add each character from group into string until hitting delimiter
        String fieldValue;
        while (fieldsString[currentIndex] != DELIMITER) {
            String currentChar = String(fieldsString[currentIndex]);
            fieldValue = String(fieldValue + currentChar);
            currentIndex ++;
        }
        
        //insert value into array and move to next character
        fieldsArray[i] = fieldValue;
        currentIndex ++;
    }
    
    //fields array full, now verify each value and add to fields matrix
    for (int i = 0; i < ENVIRO_COUNT; i++) {
        
        float previous = -999999;
        for (int j = 0; j < FIELD_COUNT; j++) {
            int currentIndex = j + (i * FIELD_COUNT);
            
            String valueString = fieldsArray[currentIndex];
            
            //try to convert input to float - controller will only allow floats
            float valueFloat = valueString.toFloat();
            
            //values must be in ascending order, within given ranges, and not null
            if ((valueFloat == NULL) && (valueString != "0")) { return false; }
            if ((valueFloat > environments[i].maxValue) || (valueFloat < environments[i].minValue)) { return false; }
            if (previous >= valueFloat) { return false; }
            
            //add validated data to matrix
            fieldsMatrix[i][j] = valueFloat;
        }
    }
    
    //Particle.publish("Filled Matrix", NULL, PRIVATE);
    
    //if input has passed all checks, then update the program to use the new input
    highSensitivity = setSensitivity;
    for (int i = 0; i < FIELD_COUNT; i++) { enviroTemp.fields[i] = fieldsMatrix[0][i]; }
    for (int i = 0; i < FIELD_COUNT; i++) { enviroHum.fields[i] = fieldsMatrix[1][i]; }
    for (int i = 0; i < FIELD_COUNT; i++) { enviroMst.fields[i] = fieldsMatrix[2][i]; }
    
    //save validated instructions to device for use in case of reboot and return
    int fd = open(FILEPATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd != -1) {
        write(fd, dataString.c_str(), dataString.length());
        close(fd); 
    }
    else { Particle.publish(ERROR_MESSAGE, "Couldn't save data to disk.", PRIVATE); }
    
    //data successfully validated, so return true
    String successString = "Successfully retrieved instructions: " + dataString;
    Particle.publish(INSTRUCTIONS_MESSAGE, successString, PRIVATE);
    return true;
}

//sets all relevant pins to output mode
void initPins() {
    pinMode(HOT_PIN, OUTPUT);    
    pinMode(COLD_PIN, OUTPUT); 
    pinMode(MST_PIN, OUTPUT);    
    pinMode(ARID_PIN, OUTPUT); 
    pinMode(WET_PIN, INPUT);    
    //pinMode(DRY_PIN, OUTPUT); 
}

//checks storage for last-saved instructions
void seekInstructions(){
    String data;
    int maxLength = (1 + 1) + (ENVIRO_COUNT * FIELD_COUNT * (1 + 3));  //expecting (1 semi-colon + up to 3 chars) for each field for each environment
    int minLength = (1 + 1) + (ENVIRO_COUNT * FIELD_COUNT * (1 + 1));
    char* buffer = new char[maxLength];
    
    int fd = open(FILEPATH, O_RDONLY);
    int actualLength;
    if (fd != -1) {
        actualLength = read(fd, buffer, maxLength);
        close(fd);
        
        //instruction length is smaller than 
        if ((actualLength < minLength) || (actualLength > maxLength)) { 
            Particle.publish(EMERGENCY_MESSAGE, "Corrupted Instructions", PRIVATE);
            return;
        }
        
        //turn received data into string and remove any excess junk
        data = String(buffer);
        data.remove(actualLength);
        
        //attempt to validate
        String message = String("Data: " + data);
        //Particle.publish("Read Data", message, PRIVATE);
        if (interpretData(data) == true) { 
            hasInstructions = true;
            return; 
        }
    }
    
    //instrutcions failed to validate, so we have no instructions
    Particle.publish(EMERGENCY_MESSAGE, "No instructions", PRIVATE);
}

//initializes a specified environment
void initEnvironent(enviroData &environment, EnviroType envType, int pin0, int pin1, float minValue, float maxValue, float defaultValue) {
    environment.type = envType;
    environment.status = INIT;
    environment.lastValue = 0;
    environment.pins[0] = pin0;
    environment.pins[1] = pin1;
    environment.minValue = minValue;
    environment.maxValue = maxValue;
    environment.defaultValue = defaultValue;    
}

//sets up the desired environmens and adds them to the array
void initEnvironments() {
    initEnvironent(enviroTemp, TEMP, COLD_PIN, HOT_PIN, 0, 50, DIGITAL_DEFAULT);
    initEnvironent(enviroHum, HUMID, ARID_PIN, MST_PIN, 0, 100, DIGITAL_DEFAULT);
    initEnvironent(enviroMst, MOIST, WET_PIN, WET_PIN, 0, 100, ANALOG_DEFAULT);
    
    environments[0] = enviroTemp;
    environments[1] = enviroHum;
    environments[2] = enviroMst;
}

//================================================================================================================================
// MAIN PROGRAM LOOP
//================================================================================================================================

//starting point for program
void setup() {
    
    //unlink(FILEPATH); //delte data for testing purposes
    Particle.publish(UPDATE_MESSAGE, "Particle On", PRIVATE);
    initEnvironments();
    initPins();
    
    //seeking instructions must occur after 
    Particle.subscribe("FromRPi", myHandler);
    if (hasInstructions == false) { seekInstructions(); }
    
	dht.begin();
	
    //make the directory required
    mkdir(DIRECTORY, 0777); //don't change 0777

    //make sure there's instructions before starting
    timePrev = Time.now();
}

//program loop
void loop() {
    
    long timeNow = Time.now();
    long difference = timeNow - timePrev;
    
    //after waiting delay, update previous time and perform operations
    if (difference < DELAY) { return; }
    timePrev = timeNow;
    
    Particle.subscribe("FromRPi", myHandler);
    if (hasInstructions == false) { return; }

    readTemp();
    delay(100);
    
    readHum();
    delay(100);
    
    readMoist();
    delay(100);
}