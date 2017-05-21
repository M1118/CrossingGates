#include <AsyncStepper.h>
#include <EEPROM.h>

/*
 * Level crossing gates
 */

#define GATE1_STEPPER   2
#define GATE2_STEPPER   6
#define SWITCH1        10
#define SWITCH2        11
#define SIGNAL1        12
#define SIGNAL2        13

#define GATE_MAGIC      0xAD15
#define RPM             10
#define STEPS_PER_REV   (64 * 64)

typedef enum {
  GateOpen,
  GateClosing,
  GateClosed,
  GateOpening
} GATE_STATE;

typedef struct {
  AsyncStepper   *motor;
  GATE_STATE   state;
  int          currentStep;
  int          closedStep;
} GATE;

GATE  gate1, gate2;

struct {
  int         gate_magic;
  GATE_STATE  gate1_state;
  GATE_STATE  gate2_state;
  int         gate1_steps;
  int         gate2_steps;
  int         gate1_percentage;
  int         gate2_percentage;
  bool        gate1_direction;
  bool        gate2_direction;
  int         gate1_position;
  int         gate2_position;
} config;

GATE_STATE  desiredState = GateOpen;
int         desiredSignal = SIGNAL1;

enum {
  NormalMode,
  SetupMode
} mode;

int setupStep = 0;

void setup() {
  // put your setup code here, to run once:
  readConfiguration();

  mode = NormalMode;
  gate1.closedStep = config.gate1_steps;
  gate2.closedStep = config.gate2_steps;

  gate1.motor = new AsyncStepper(STEPPER_MODE_CONSTRAINED, config.gate1_steps, STEPS_PER_REV, RPM,
                                 GATE1_STEPPER, GATE1_STEPPER + 1, GATE1_STEPPER + 2, GATE1_STEPPER + 3);
  gate1.motor->setCurrentPosition(config.gate1_position);
  gate2.motor = new AsyncStepper(STEPPER_MODE_CONSTRAINED, config.gate2_steps, STEPS_PER_REV, RPM,
                                 GATE2_STEPPER, GATE2_STEPPER + 1, GATE2_STEPPER + 2, GATE2_STEPPER + 3);
  gate2.motor->setCurrentPosition(config.gate2_position);
  pinMode(SWITCH1, INPUT_PULLUP);
  pinMode(SWITCH2, INPUT_PULLUP);
  pinMode(SIGNAL1, OUTPUT);
  pinMode(SIGNAL2, OUTPUT);
  digitalWrite(SIGNAL1, HIGH);
  digitalWrite(SIGNAL2, HIGH);

  gate1.motor->setActive(true);
  gate2.motor->setActive(true);

  gate1.state = config.gate1_state;
  gate2.state = config.gate1_state;

  Serial.begin(9600);
  Serial.println("Crossing Gates");
  Serial.println(mode == NormalMode ? "Operational mode" : "Setup mode");
  doDebug();
}

void readConfiguration()
{
  int           i;
  unsigned char *ptr = (unsigned char *)&config;

  for (i = 0; i < sizeof(config); i++)
  {
    *ptr++ = EEPROM.read(i);
  }
  if (config.gate_magic != GATE_MAGIC)
  {
    defaultConfiguration();
  }
}

void writeConfiguration()
{
  int           i;
  unsigned char *ptr = (unsigned char *)&config;

  for (i = 0; i < sizeof(config); i++)
  {
    EEPROM.write(i, *ptr++);
  }
}

void writePosition(int gateNo)
{
  int           i;
  unsigned char *ptr = (unsigned char *)&config;
  unsigned char *ptr2 = (unsigned char *) & (config.gate1_position);

  if (gateNo == 2)
  {
    ptr2 += sizeof(int);
  }
  unsigned int offset = ptr2 - ptr;
  for (i = 0; i < sizeof(int); i++)
  {
    EEPROM.write(offset + i, *ptr2++);
  }
}

void defaultConfiguration()
{
  config.gate_magic = GATE_MAGIC;
  config.gate1_steps = STEPS_PER_REV / 4;
  config.gate2_steps = STEPS_PER_REV / 4;
  config.gate1_state = GateOpen;
  config.gate2_state = GateOpen;
  config.gate1_percentage = 50;
  config.gate2_percentage = 50;
  config.gate1_direction = true;
  config.gate2_direction = true;
  config.gate1_position = 0;
  config.gate2_position = 0;
  writeConfiguration();
}

void notifyStepperPosition(AsyncStepper *stepper, unsigned int step)
{
  if (stepper == gate1.motor)
  {
    gate1.currentStep = step;
    config.gate1_position = step;
    if (step == 0 && config.gate1_direction)
    {
      Serial.println("Gate1 open");
      gate1.state = GateOpen;
    }
    else if (step == gate1.closedStep  && config.gate1_direction)
    {
      Serial.println("Gate1 closed");
      gate1.state = GateClosed;
    }
    else if (step == 0 && (! config.gate1_direction))
    {
      Serial.println("Gate1 closed");
      gate1.state = GateClosed;
    }
    else if (step == gate1.closedStep  && (! config.gate1_direction))
    {
      Serial.println("Gate1 open");
      gate1.state = GateOpen;
    }
    else
    {
      writePosition(1);
    }
  }
  else if (stepper == gate2.motor)
  {
    gate2.currentStep = step;
    config.gate2_position = step;
    if (step == 0 && config.gate2_direction)
    {
      Serial.println("Gate2 open");
      gate2.state = GateOpen;
    }
    else if (step == gate2.closedStep  && config.gate2_direction)
    {
      Serial.println("Gate2 closed");
      gate2.state = GateClosed;
    }
    else if (step == 0 && (! config.gate2_direction))
    {
      Serial.println("Gate2 closed");
      gate2.state = GateClosed;
    }
    else if (step == gate2.closedStep  && (! config.gate2_direction))
    {
      Serial.println("Gate2 open");
      gate2.state = GateOpen;
    }
    else
    {
      writePosition(2);
    }
  }
  if (config.gate1_state != gate1.state || config.gate2_state != gate2.state)
  {
    config.gate1_state = gate1.state;
    config.gate2_state = gate2.state;
    writeConfiguration();
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  gate1.motor->loop();
  gate2.motor->loop();

  processSerial();

  if (mode == SetupMode)
  {
    return;
  }

  GATE_STATE prevState = desiredState;
  int sw1 = digitalRead(SWITCH1);
  int sw2 = digitalRead(SWITCH2);

  if (sw1 == LOW && sw2 == LOW)
  {
    desiredState = GateOpen;
    Serial.println("Error both signals off");
    // Error state
  }
  else if (sw1 == LOW && sw2 == HIGH)
  {
    desiredState = GateOpen;
    desiredSignal = SIGNAL1;
  }
  else if (sw1 == HIGH && sw2 == LOW)
  {
    desiredState = GateOpen;
    desiredSignal = SIGNAL2;
  }
  else if (sw1 == HIGH && sw2 == HIGH)
  {
    desiredState = GateClosed;
  }
  if (desiredState == GateClosed)
  {
    digitalWrite(SIGNAL1, HIGH);
    digitalWrite(SIGNAL2, HIGH);
    if (gate1.state == GateOpen)
    {
      Serial.println("Closing gate 1");
      gate1.state = GateClosing;
      gate1.motor->setActive(true);
      gate1.motor->setSpeed(config.gate1_percentage, config.gate1_direction);
    } else if (gate1.state == GateClosed && gate2.state == GateOpen)
    {
      Serial.println("Closing gate 2");
      gate2.state = GateClosing;
      gate2.motor->setActive(true);
      gate1.motor->setActive(false);
      gate2.motor->setSpeed(config.gate2_percentage, config.gate2_direction);
    } else if (gate1.state == GateClosed && gate2.state == GateClosed)
    {
      gate2.motor->setActive(false);
    }
  }
  if (desiredState == GateOpen)
  {
    if (gate2.state == GateClosed)
    {
      Serial.println("Opening gate 2");
      gate2.state = GateOpening;
      gate2.motor->setActive(true);
      gate2.motor->setSpeed(config.gate2_percentage, config.gate2_direction ? false : true);
    } else if (gate1.state == GateClosed && gate2.state == GateOpen)
    {
      Serial.println("Opening gate 1");
      gate1.state = GateOpening;
      gate1.motor->setActive(true);
      gate2.motor->setActive(false);
      gate1.motor->setSpeed(config.gate1_percentage, config.gate1_direction ? false : true);
    } else if (gate1.state == GateOpen && gate2.state == GateOpen)
    {
      digitalWrite(desiredSignal, LOW);
      gate1.motor->setActive(false);
    }
  }

  if (prevState != desiredState)
  {
    Serial.print("State change -> ");
    switch (desiredState)
    {
      case GateOpen:
        Serial.println("open");
        break;
      case GateClosed:
        Serial.println("closed");
        break;
    }
    doDebug();
  }
}

#define SERIAL_BUFFER_SIZE 80
char serialBuffer[SERIAL_BUFFER_SIZE];
int serialCursor = 0;
#define SERIAL_PROMPT "> "

void processSerial()
{
  if (Serial.available()) {
    char ch = Serial.read();
    if (mode == NormalMode)
    {
      serialBuffer[serialCursor++] = ch;
      Serial.print(ch);
      if (serialCursor == SERIAL_BUFFER_SIZE) {
        serialCursor = 0;
        Serial.println();
        Serial.print(SERIAL_PROMPT);
        return;
      }
      if (ch == '\n' || ch == '\r') {
        serialBuffer[serialCursor > 0 ? (serialCursor - 1) : 0] = 0; // Overwrite with a null
        while (Serial.available()) {
          Serial.read();
        }
        Serial.println();

        if (strncasecmp(serialBuffer, "debug", 5) == 0) {
          doDebug();
        } else if (strncasecmp(serialBuffer, "save", 4) == 0) {
          writeConfiguration();
        } else if (strncasecmp(serialBuffer, "default", 7) == 0) {
          defaultConfiguration();
        } else if (strncasecmp(serialBuffer, "print", 5) == 0) {
          printConfiguration();
        } else if (strncasecmp(serialBuffer, "setup", 5) == 0) {
          doSetup();
        } else if (strncasecmp(serialBuffer, "rpm", 3) == 0) {
          doRPM(serialBuffer + 3);
        } else if (strlen(serialBuffer) == 0 || strncasecmp(serialBuffer, "help", 4) == 0) {
          doHelp();
        } else {
          Serial.print("Unrecognised command: ");
          Serial.println(serialBuffer);
        }

        if (mode == NormalMode)
        {
          Serial.println();
          Serial.print(SERIAL_PROMPT);
          serialCursor = 0;
        }
      }
    } else {
      doSetupChar(ch);
    }
  }
}

void doRPM(char *buf)
{
  while (*buf && *buf == ' ')
    buf++;
  int rpm = atoi(buf);
  if (rpm)
  {
    Serial.println();
    Serial.print("Set RPM to ");
    Serial.println(rpm);
    gate1.motor->setRPM(rpm);
    gate2.motor->setRPM(rpm);
  }
}

void doSetup()
{
  Serial.println("Entering setup mode");
  Serial.println("Gate 1");
  mode = SetupMode;
  setupStep = 1;
  Serial.print("Current direction ");
  Serial.print(config.gate1_direction ? "clockwise" : "anti-clockwise");
  Serial.print(". Change direction (y/n): ");
}

unsigned int stepDelta;
AsyncStepper *deltaMotor;

void doSetupChar(char ch)
{
  switch (setupStep)
  {
    case 1:
      if (ch == 'y' || ch == 'Y')
      {
        Serial.println("yes");
        config.gate1_direction = ! config.gate1_direction;
        setupStep++;
      } else if (ch == 'n' || ch == 'N')
      {
        Serial.println("no");
        setupStep++;
      }
      if (setupStep != 1)
      {
        Serial.println("Set gate 1 speed");
        Serial.print("Current speed ");
        Serial.print(config.gate1_percentage);
        Serial.print(", enter new speed: ");
        serialCursor = 0;
      }
      break;
    case 2:
      serialBuffer[serialCursor++] = ch;
      Serial.print(ch);
      if (serialCursor == SERIAL_BUFFER_SIZE) {
        serialCursor = 0;
        Serial.println();
        Serial.print("Current speed ");
        Serial.print(config.gate1_percentage);
        Serial.print(", enter new speed: ");
        return;
      }
      if (ch == '\n' || ch == '\r') {
        serialBuffer[serialCursor > 0 ? (serialCursor - 1) : 0] = 0; // Overwrite with a null
        while (Serial.available()) {
          Serial.read();
        }
        int newSpeed = atoi(serialBuffer);
        if (newSpeed > 0)
        {
          config.gate1_percentage = newSpeed;
        }
        Serial.println();
        setupStep++;
        Serial.println("Gate 2");
        Serial.print("Current direction ");
        Serial.print(config.gate2_direction ? "clockwise" : "anti-clockwise");
        Serial.print(". Change direction (y/n): ");
      }
      break;
    case 3:
      if (ch == 'y' || ch == 'Y')
      {
        Serial.println("yes");
        config.gate2_direction = ! config.gate2_direction;
        setupStep++;
      } else if (ch == 'n' || ch == 'N')
      {
        Serial.println("no");
        setupStep++;
      }
      if (setupStep != 3)
      {
        Serial.println("Set gate 2 speed");
        Serial.print("Current speed ");
        Serial.print(config.gate2_percentage);
        Serial.print(", enter new speed: ");
        serialCursor = 0;
      }
      break;
    case 4:
      serialBuffer[serialCursor++] = ch;
      Serial.print(ch);
      if (serialCursor == SERIAL_BUFFER_SIZE) {
        serialCursor = 0;
        Serial.println();
        Serial.print("Current speed ");
        Serial.print(config.gate2_percentage);
        Serial.print(", enter new speed: ");
        return;
      }
      if (ch == '\n' || ch == '\r') {
        serialBuffer[serialCursor > 0 ? (serialCursor - 1) : 0] = 0; // Overwrite with a null
        while (Serial.available()) {
          Serial.read();
        }
        int newSpeed = atoi(serialBuffer);
        if (newSpeed > 0)
        {
          config.gate2_percentage = newSpeed;
        }
        Serial.println();
        setupStep++;
        setupText(1, "open");
        gate1.motor->setActive(true);
        gate1.motor->setSpeed(config.gate1_percentage, config.gate1_direction);
        deltaMotor = gate1.motor;
        stepDelta = 0;
      }
      break;

    case 5:
    case 6:
    case 7:
    case 8:
      if (ch == 'C')
      {
        stepDelta += 10;
        deltaMotor->stepN(10, true);
      }
      else if (ch == 'c')
      {
        stepDelta += 5;
        deltaMotor->stepN(5, true);
      }
      else if (ch == '+')
      {
        stepDelta++;
        deltaMotor->stepN(1, true);
      }
      if (ch == 'A')
      {
        stepDelta -= 10;
        deltaMotor->stepN(10, false);
      }
      else if (ch == 'a')
      {
        stepDelta -= 5;
        deltaMotor->stepN(5, false);
      }
      else if (ch == '-')
      {
        stepDelta--;
        deltaMotor->stepN(1, false);
      }
      else if (ch == '\r')
      {
        switch (setupStep)
        {
          case 5:
            if (config.gate1_direction)
            {
              config.gate1_steps += stepDelta;
              config.gate1_position += stepDelta;
              deltaMotor->setMaxSteps(config.gate1_steps);
              deltaMotor->setCurrentPosition(config.gate1_position);
            }
            else
            {
              config.gate1_steps -= stepDelta;
              config.gate1_position = 0;
              deltaMotor->setMaxSteps(config.gate1_steps);
              deltaMotor->setCurrentPosition(0);
            }
            setupStep++;
            setupText(1, "closed");
            gate1.motor->setActive(true);
            gate1.motor->setSpeed(config.gate1_percentage, ! config.gate1_direction);
            deltaMotor = gate1.motor;
            stepDelta = 0;
            break;
          case 6:
            if (! config.gate1_direction)
            {
              config.gate1_steps += stepDelta;
              config.gate1_position += stepDelta;
              deltaMotor->setMaxSteps(config.gate1_steps);
              deltaMotor->setCurrentPosition(config.gate1_position);
            }
            else
            {
              config.gate1_steps -= stepDelta;
              config.gate1_position = 0;
              deltaMotor->setMaxSteps(config.gate1_steps);
              deltaMotor->setCurrentPosition(0);
            }
            gate1.motor->setSpeed(config.gate1_percentage, config.gate1_direction);
            while (gate1.state != GateOpen)
                gate1.motor->loop();
            setupStep++;
            setupText(2, "open");
            gate2.motor->setActive(true);
            gate2.motor->setSpeed(config.gate2_percentage, config.gate2_direction);
            deltaMotor = gate2.motor;
            stepDelta = 0;
            break;
          case 7:
            if (config.gate2_direction)
            {
              config.gate2_steps += stepDelta;
              config.gate1_position += stepDelta;
              deltaMotor->setMaxSteps(config.gate2_steps);
              deltaMotor->setCurrentPosition(config.gate2_position);
            }
            else
            {
              config.gate2_steps -= stepDelta;
              config.gate2_position = 0;
              deltaMotor->setMaxSteps(config.gate2_steps);
              deltaMotor->setCurrentPosition(0);
            }
            setupStep++;
            setupText(2, "closed");
            gate2.motor->setActive(true);
            gate2.motor->setSpeed(config.gate2_percentage, ! config.gate2_direction);
            deltaMotor = gate2.motor;
            stepDelta = 0;
            break;
          case 8:
            if (! config.gate2_direction)
            {
              config.gate2_steps += stepDelta;
              config.gate1_position += stepDelta;
              deltaMotor->setMaxSteps(config.gate2_steps);
              deltaMotor->setCurrentPosition(config.gate2_position);
            }
            else
            {
              config.gate2_steps -= stepDelta;
              config.gate2_position = 0;
              deltaMotor->setMaxSteps(config.gate2_steps);
              deltaMotor->setCurrentPosition(0);
            }
            gate2.motor->setSpeed(config.gate2_percentage, config.gate2_direction);
            while (gate2.state != GateOpen)
                gate2.motor->loop();
            setupStep++;
        }
        writeConfiguration();
      }
      else if (ch == 0x1b)  // Escape
      {
        deltaMotor->stepN(stepDelta, stepDelta > 0 ? false : true);
        stepDelta = 0;
      }
      break;

    case 9:
      Serial.println("Operational mode");
      mode = NormalMode;
      Serial.println();
      Serial.print(SERIAL_PROMPT);
      break;
  }
}

void setupText(int gate, char *position)
{
  Serial.print("Setup ");
  Serial.print(position);
  Serial.print(" for gate ");
  Serial.println(gate);
  Serial.println("Wait for gate to stop moving and then press c to move the gate clockwise and a to move it anti-clockwise");
  Serial.println("Uppercase C and A will move a larger amount than lowercase and the + and - keys allow for small adjustments");
  Serial.println("Press 'Return' when complete or 'Escape' to abandon correction.");
}

void doHelp()
{
  Serial.println();
  Serial.println("Level crossing gate setup.");
  Serial.println("   default        - Reset to default settings");
  Serial.println("   save           - Write configuration to EEPROM");
  Serial.println("   setup          - Setup gate parameters");
  Serial.println("   print          - Print the current configuration");
  Serial.println("   help           - Print this help information");
}




void doDebug()
{
  Serial.println("Gate 1");
  Serial.print  ("      state:     ");
  switch (gate1.state)
  {
    case GateOpen:
      Serial.println("Open");
      break;
    case GateOpening:
      Serial.println("Opening");
      break;
    case GateClosed:
      Serial.println("Closed");
      break;
    case GateClosing:
      Serial.println("Closing");
      break;
  }
  Serial.print  ("   position:     ");
  Serial.println(gate1.currentStep);
  Serial.print  ("closed step:     ");
  Serial.println(gate1.closedStep);
  Serial.print  ("      speed:     ");
  Serial.println(config.gate1_percentage);
  Serial.print  ("   direction:    ");
  Serial.println(config.gate1_direction ? "clockwise" : "anti - clockwise");
  Serial.print("Motor interval ");
  Serial.print(gate1.motor->getInterval());
  Serial.println("uS");
  Serial.println("Gate 2");
  Serial.print  ("      state:     ");
  switch (gate2.state)
  {
    case GateOpen:
      Serial.println("Open");
      break;
    case GateOpening:
      Serial.println("Opening");
      break;
    case GateClosed:
      Serial.println("Closed");
      break;
    case GateClosing:
      Serial.println("Closing");
      break;
  }
  Serial.print  ("   position:     ");
  Serial.println(gate2.currentStep);
  Serial.print  ("closed step:     ");
  Serial.println(gate2.closedStep);
  Serial.print  ("      speed:     ");
  Serial.println(config.gate2_percentage);
  Serial.print  ("   direction:    ");
  Serial.println(config.gate2_direction ? "clockwise" : "anti - clockwise");
  Serial.print("Motor interval ");
  Serial.print(gate2.motor->getInterval());
  Serial.println("uS");
}

void printConfiguration()
{
  Serial.println("Gate 1");
  Serial.print  ("      steps:     ");
  Serial.println(config.gate1_steps);
  Serial.print  ("      speed:     ");
  Serial.println(config.gate1_percentage);
  Serial.print  ("   direction:    ");
  Serial.println(config.gate1_direction ? "clockwise" : "anti - clockwise");
  Serial.println("Gate 2");
  Serial.print  ("      steps:     ");
  Serial.println(config.gate2_steps);
  Serial.print  ("      speed:     ");
  Serial.println(config.gate2_percentage);
  Serial.print  ("   direction:    ");
  Serial.println(config.gate2_direction ? "clockwise" : "anti - clockwise");
}
