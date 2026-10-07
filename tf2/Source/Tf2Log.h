#pragma once




namespace ns_tf2 {

void logInit();
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logShutdown();

} 
