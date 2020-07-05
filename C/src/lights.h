#ifndef __LIGHTS_H__
#define __LIGHTS_H__

void ledInit(void);
void getLedSta(unsigned char disWarning, unsigned char client_Connected);
void GRB_Set(unsigned char gdat,unsigned char rdat,unsigned char bdat);
void GRB_MultiColour_work(unsigned int ledNum, int brightness );
void GRBInit();
void GRB_work(unsigned int ledNum, unsigned long colour, int brightness );

unsigned long getColor();
unsigned int getBrightness();
void setColor(unsigned long _color);
void setBrightness(unsigned int _brightness);

#endif /* __LIGHTS_H__ */
