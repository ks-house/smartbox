#pragma once
#include <string>
#include <cstring>
#include <ctime>
#include <cstdint>
class String:public std::string {
 public:
 using std::string::string;
 String()=default;String(const std::string& s):std::string(s){}
 String(int n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}
 int indexOf(const char* s,int from=0)const{auto n=find(s,from);return n==npos?-1:int(n);}
 int indexOf(const std::string& s,int from=0)const{auto n=find(s,from);return n==npos?-1:int(n);}
 int indexOf(char c,int from=0)const{auto n=find(c,from);return n==npos?-1:int(n);}
 String substring(int a,int b)const{return substr(a,b-a);}
};
unsigned long millis();void delay(unsigned long);
bool getLocalTime(struct tm*,uint32_t);
struct ESPFixture {void restart();};extern ESPFixture ESP;
