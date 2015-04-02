Oscii
=====

oscii is a basic oscilloscope for serial inputs, such as Arduino/R-Pi.

It relies on SDL2 for rendering.

![screenshot](oscii.png?raw=true)

## Sample arduino signal generator ##

The following arduino code uses digital pin 13 to generate a signal fed into
analog-0.

~~~c
#define ANALOG_IN 0                                                             
int outPin=13;                                                                  
int outPinState = LOW;                                                          
int count = 1;                                                                  
int every = 1000;
                                                                                
void setup() {                                                                  
  //Serial.begin(9600);                                                         
  Serial.begin(115200);
  pinMode(outPin, OUTPUT);
  digitalWrite(outPin, LOW);
}                                                                               
                                                                                
void loop() {                                                                   
  int val = analogRead(ANALOG_IN);                                              
  Serial.write( 0xff );                                                         
  Serial.write( (val >> 8) & 0xff );                                            
  Serial.write( val & 0xff );                                                   
                                                                                
  /* Generate signal to test oscilloscope */
  if (count == every) {
    if (outPinState == LOW)
      outPinState = HIGH;
    else
      outPinState = LOW;
    count = 0;
  }
  digitalWrite(outPin, outPinState);
  count++;
}
~~~
