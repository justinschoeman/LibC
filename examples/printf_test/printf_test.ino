#include <LibC.h>

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println("Starting....");
  pprintf(Serial, "Use Serial explicitly as a print device...\n");
  printf_setprint(&Serial);
  printf("Use Serial by setting it as the system print device...\n");
  delay(5000);
}


void loop()
{
  float sig = rand()-RAND_MAX/2;
  float expn = rand()%150-75.0;
  int width = rand()%50-25;
  int prec = rand()%20;
  float num = sig * powf(10.0, expn);
  printf("sig:%.0f exp:%.0f width:%d prec:%d\n", sig, expn, width, prec);
  printf("g:%g\n", num);
  printf("g**:%*.*g\n", width, prec, num);
  printf("f:%f\n", num);
  printf("f**:%*.*f\n", width, prec, num);
  printf("e:%e\n", num);
  printf("e**:%*.*e\n", width, prec, num);
}

