#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

void initWebServer();
void startWebServer();
void stopWebServer();
void dir(bool op);
void delete_file(bool op, String filename);

#endif