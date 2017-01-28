#ifndef CONTROL_H
#define CONTROL_H

typedef unsigned char controlID_t;
typedef unsigned char hardwareControlID_t;
typedef unsigned char protocol_t;
typedef unsigned short address_t;
typedef short speed_t;
typedef unsigned short locoID_t;

enum controlIDs : controlID_t {
  CONTROL_ID_CONSOLE = 0,
  CONTROL_ID_HARDWARE,
  CONTROL_ID_WEBSERVER
};

class Control {
  public:
    Control(controlID_t control_id);
    virtual ~Control() {};
    controlID_t getControlID();
		virtual void locoSpeed(const controlID_t controlID, const locoID_t locoID, const speed_t speed);
  private:
    controlID_t controlID;
};

inline controlID_t Control::getControlID() {
  return controlID;
}

#endif // CONTROL_H

