#ifndef WEB_HANDLER_H
#define WEB_HANDLER_H
#include <WebServer.h>

void setupWeb();
void handleRoot();
void handleToggle();
void handleSetInterval();
void handleSetAlerts();
void handleRetry();
void handleNotFound();

#endif