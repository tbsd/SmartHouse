#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Time.h>

template <typename T, typename U> 
class Pair {
  public:
  T first;
  U second;

  Pair() {};
  Pair(T first, U second) : first(first), second(second) {};

  bool operator==(const Pair<T, U> &other) const {
    return first == other.first && second == other.second;
  }

  bool operator!=(const Pair<T, U> &other) const {
    return !(*this == other);
  }
};


template <typename T, size_t maxSize>
class LimitedList {
  T data[maxSize];
  int last = -1;

  public:
  LimitedList() {};

  LimitedList(T a, T b, T c) : last(maxSize - 1) {
    data[0] = a;
    data[1] = b;
    data[2] = c;
  };

  void push_back(T elem) {
    if (last == maxSize - 1) {
      for (int i = 0; i < maxSize - 1; ++i)
        data[i] = data[i + 1];
      data[maxSize - 1] = elem;
    } else
      data[++last] = elem;
  }

  void clear() {last = -1;}

  bool operator==(const LimitedList &other) const {
    if (last != other.last)
      return false;
    for (int i = 0; i <= last; ++i)
      if (data[i] != other.data[i])
        return false;
    return true;
  }

  bool operator!=(const LimitedList &other) const {
    return !(*this == other);
  }

  T operator[] (int i) {
    return data[i];
  }

  bool isFull() {
    return last == maxSize - 1;
  }

  bool isEmpty() {
    return last == -1;
  }

  int size() {
    return last + 1;
  }
};

const char SSID[] = "********************";
const char PASSWORD[] = "********************";
IPAddress IP(192,168,0,10);
IPAddress GATEWAY(192,168,0,1);
IPAddress SUBNET(255,255,255,0);
const int SENSOR1 = 16;
const int SENSOR2 = 5;
const int LIGHTS = 14;
LimitedList<Pair<bool, bool>, 3> COMING_IN (Pair<bool, bool>(false, true), Pair<bool, bool>(false, false), Pair<bool, bool>(true, false));
LimitedList<Pair<bool, bool>, 3> COMING_OUT(Pair<bool, bool>(true, false), Pair<bool, bool>(false, false), Pair<bool, bool>(false, true));
bool isOn = true;

int PEOPLE_IN_ROOM = 0;
LimitedList<Pair<bool, bool>, 3> STATE;
ESP8266WebServer SERVER(80);
unsigned long SLEEP_START;
const unsigned long SLEEP_TIME = 10 * 60 * 60;

void(* resetFunc) (void) = 0;
void checkState();
void switchLights();
void nightMode();

void setup() {
  pinMode(LIGHTS, OUTPUT);
  pinMode(SENSOR1, INPUT);
  pinMode(SENSOR2, INPUT);
 
  digitalWrite(LIGHTS, LOW);

  WiFi.begin(SSID, PASSWORD);
  WiFi.config(IP, GATEWAY, SUBNET);
  SERVER.on("/switch", [](){
    nightMode();
    SERVER.send(200, "text/html", isOn ? "Have a good day" : "Good night");
      });
  SERVER.on("/hard_switch", [](){
    switchLights();
    SERVER.send(200, "text/html", isOn ? "Lights on" : "Lights off");
      });
  SERVER.on("/status", [](){
    char pplCount[3] = "";
    sprintf(pplCount, "%d", PEOPLE_IN_ROOM);
    char stateStr[7];
    for (int i = 0; i < 3; ++i) {
      stateStr[i * 2] = '0' + STATE[i].first;
      stateStr[i * 2 + 1] = '0' + STATE[i].second;
    }
    stateStr[6] = '\0';
    std::string response("People in room: " + std::string(pplCount) + "; \n" + 
    "Light: " + (digitalRead(LIGHTS) ? " on; \n" : " off; \n") +
    "First sensor: " + (!digitalRead(SENSOR1) ? "on; \n" : "off; \n") +
    "Second sensor: " + (!digitalRead(SENSOR2) ? "on; \n" : "off; \n") +
    "isOn: " + (isOn ? " on; \n" : " off; \n") +
    "State: " + stateStr);
    SERVER.send(200, "text/html", response.c_str());
      });
  SERVER.on("/reset", [](){
    SERVER.send(200, "text/html", "Reseting...");
    delay(20);
    resetFunc();
      });
  SERVER.begin();
}

void loop() {
  Pair<bool, bool> cur(!digitalRead(SENSOR1), !digitalRead(SENSOR2));
  if (cur.first && cur.second) {
    if (STATE.size() == 3) {
      onTripwireTeared();
      STATE.clear();
    } else
      STATE.clear();
  } else if (STATE[STATE.size() - 1] != cur)
    STATE.push_back(cur);
  if (SLEEP_START + SLEEP_TIME < now())
    isOn = true;
  SERVER.handleClient();
  delay(15);
}

void nightMode() {
  if (isOn) {
    digitalWrite(LIGHTS, LOW);
    isOn = false;
    SLEEP_START = now();
  } else {
    if (PEOPLE_IN_ROOM > 0)
      digitalWrite(LIGHTS, HIGH);
    isOn = true;
  }
}

void switchLights() {
  if (digitalRead(LIGHTS)) {
    digitalWrite(LIGHTS, LOW);
  } else {
    digitalWrite(LIGHTS, HIGH);
    isOn = true;
  }
}

void onTripwireTeared() {
  if (STATE == COMING_IN) 
    ++PEOPLE_IN_ROOM;
  else if (STATE == COMING_OUT && PEOPLE_IN_ROOM > 0)
    --PEOPLE_IN_ROOM;

  if (isOn && PEOPLE_IN_ROOM > 0)
    digitalWrite(LIGHTS, HIGH);
  else
    digitalWrite(LIGHTS, LOW);
}
