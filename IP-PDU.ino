#include <SPI.h>
#include <Ethernet.h>
#include <RemoteTransmitter.h>
#include <Shell.h>
#include <LoginShell.h>
#include <EEPROMex.h>

#define PASSWORD_SIZE_ADDR 0
#define PASSWORD_START_ADDR 1
#define MAX_PASSWORD_SIZE 14
#define CUSTOM_NETWORK_ADDR 15
#define IP_START_ADDR 16
#define MASK_START_ADDR 22
#define GW_START_ADDR 28

#define CONFIG_SIZE_ADDR 255
#define CONFIG_START_ADDR 256
#define MAX_DEVNAME_SIZE 11
#define MAX_UNITS 16
#define TRANSMITTER_PIN 12

typedef struct {
  byte id;
  char name[MAX_DEVNAME_SIZE];
  byte type;
  byte address;
  byte device;
  byte state;
} unit;

byte config_size;
unit dev_config[MAX_UNITS];
  
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};

IPAddress ip(192, 168, 2, 2);
IPAddress mask(255, 255, 255, 0);
IPAddress gw(192, 168, 2, 1);

EthernetServer server(23);
EthernetClient client;
bool haveClient = false;

LoginShell shell;

ActionTransmitter actionTransmitter(TRANSMITTER_PIN);
KaKuTransmitter kakuTransmitter(TRANSMITTER_PIN);
BlokkerTransmitter blokkerTransmitter(TRANSMITTER_PIN);
ElroTransmitter elroTransmitter(TRANSMITTER_PIN);

void load_config() {
  byte current_config_size = EEPROM.readByte(CONFIG_SIZE_ADDR);
  if (current_config_size == 255){
    config_size = 0;
    EEPROM.updateByte(CONFIG_SIZE_ADDR, config_size);
  } else {
    config_size = current_config_size;
  }
  unit current_config[config_size];
  EEPROM.readBlock<unit>(CONFIG_START_ADDR, current_config, config_size);  
  for(byte i = 0; i < config_size; i++){
    dev_config[i] = current_config[i];
  }
}

void load_network() {
  byte custom_network_flag = EEPROM.readByte(CUSTOM_NETWORK_ADDR);
  if (custom_network_flag != 255){
    EEPROM.readBlock<IPAddress>(IP_START_ADDR, ip);
    EEPROM.readBlock<IPAddress>(MASK_START_ADDR, mask);
    EEPROM.readBlock<IPAddress>(GW_START_ADDR, gw);
  }
}

char * load_password() {
  byte current_password_size = EEPROM.readByte(PASSWORD_SIZE_ADDR);
  if (current_password_size == 255){
    return "admin";
  } else {
    char password[current_password_size];
    EEPROM.readBlock<char>(PASSWORD_START_ADDR, password, current_password_size);
    return password;
  }  
}

int login(const char *username, const char *password) {
  char *current_password = load_password();
  if (strcmp(username, "admin") == 0) {
    if (strcmp(password, current_password) == 0) {
      return 0;
    }
  }
  return -1;
}

void cmdPasswd(Shell &shell, int argc, const ShellArguments &argv) {
  if (argc == 4){
    char *current_password = load_password();
    if (strcmp(argv[1], current_password) == 0) {
      if (strcmp(argv[2], argv[3]) == 0) {
        byte password_size = strlen(argv[2]) + 1;
        if (password_size <= MAX_PASSWORD_SIZE){
          EEPROM.updateByte(PASSWORD_SIZE_ADDR, password_size);
          EEPROM.updateBlock<char>(PASSWORD_START_ADDR, argv[2], password_size);
          server.print(F("Password changed, new password is: "));
          server.println(argv[2]);
        } else {
          server.print(F("Password too long, max password size is ")); 
          server.print(MAX_PASSWORD_SIZE-1);
          server.println(F(" characters"));
        }
      } else {
        server.println(F("New passwords do not match"));
      }
    } else {
      server.println(F("Incorrect current password"));
    }
  } else {
    server.println(F("Change password, usage: passwd <old_password> <new_password> <repeat_new_password>"));
  }
}

void cmdAdd(Shell &shell, int argc, const ShellArguments &argv) {
  if (argc == 5){
    byte name_size = strlen(argv[1]) + 1;
    if (name_size <= MAX_DEVNAME_SIZE){
      unit new_unit;
      byte current_config_size = EEPROM.readByte(CONFIG_SIZE_ADDR);
      new_unit.id = current_config_size;
      strcpy(new_unit.name, argv[1]);
      new_unit.type = byte(atol(argv[2]));
      new_unit.address = byte(atol(argv[3]));
      new_unit.device = byte(atol(argv[4]));
      new_unit.state = 0;
      dev_config[new_unit.id] = new_unit;
      EEPROM.updateByte(CONFIG_SIZE_ADDR, current_config_size + 1);
      EEPROM.updateBlock<unit>(CONFIG_START_ADDR + sizeof(unit)*new_unit.id, new_unit);
      server.println(F("New unit added"));
    } else {
      server.print(F("Name too long, max name size is ")); 
      server.print(MAX_DEVNAME_SIZE-1);
      server.println(F(" characters"));      
    }
  } else {
    server.println(F("Add unit, usage: add <name> <type(Action=0/KaKu=1/Blokker=2/Elro=3)> <address(0-255)> <device(0-255)>"));
  }
}

void cmdRm(Shell &shell, int argc, const ShellArguments &argv) {
  if (argc == 2){
    byte id = byte(atol(argv[1]));
    byte current_config_size = EEPROM.readByte(CONFIG_SIZE_ADDR);
    if (id < current_config_size){
      for ( int i = id; i < current_config_size; i++ ) {
        dev_config[i] = dev_config[i+1];
        dev_config[i].id -= 1;
      }
      current_config_size -= 1;
      EEPROM.updateByte(CONFIG_SIZE_ADDR, current_config_size);
      EEPROM.updateBlock<unit>(CONFIG_START_ADDR, dev_config, current_config_size);
      server.println(F("Unit removed"));
    } else {
      server.println(F("ID does not exist"));
    }
  } else {
    server.println(F("Remove unit, usage: rm <id>"));
  }
}

void cmdLs(Shell &shell, int argc, const ShellArguments &argv) {
  server.println(F("Current units:"));
  server.println(F("id name type address device state"));
  byte current_config_size = EEPROM.readByte(CONFIG_SIZE_ADDR);
  for (byte i = 0; i < current_config_size; i++){
    server.print(dev_config[i].id);
    server.print(F(" "));
    server.print(dev_config[i].name);
    server.print(F(" "));
    server.print(dev_config[i].type);
    server.print(F(" "));
    server.print(dev_config[i].address);
    server.print(F(" "));
    server.print(dev_config[i].device);
    server.print(F(" "));
    server.println(dev_config[i].state);
  }
}

void cmdOn(Shell &shell, int argc, const ShellArguments &argv) {
  if (argc == 2){
    byte id = byte(atol(argv[1]));
    dev_config[id].state = 1;
    EEPROM.updateBlock<unit>(CONFIG_START_ADDR + sizeof(unit)*id, dev_config[id]);          
    if (dev_config[id].type == 0) {    
      actionTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,true);
    } else if (dev_config[id].type == 1) {
      kakuTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,true);
    } else if (dev_config[id].type == 2) {
      blokkerTransmitter.sendSignal(dev_config[id].address,true);
    } else if (dev_config[id].type == 3) {
      elroTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,true);
    }
    server.println(F("Unit powered on"));
  } else {
    server.println(F("Power On unit, usage: on <id>"));
  }
}

void cmdOff(Shell &shell, int argc, const ShellArguments &argv) {
  if (argc == 2){
    byte id = byte(atol(argv[1]));
    dev_config[id].state = 0;
    EEPROM.updateBlock<unit>(CONFIG_START_ADDR + sizeof(unit)*id, dev_config[id]);          
    if (dev_config[id].type == 0) {
      actionTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,false);
    } else if (dev_config[id].type == 1) {
      kakuTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,false);
    } else if (dev_config[id].type == 2) {
      blokkerTransmitter.sendSignal(dev_config[id].address,false);
    } else if (dev_config[id].type == 3) {
      elroTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,false);
    }
    server.println(F("Unit powered off"));
  } else {
    server.println(F("Power Off unit, usage: off <id>"));
  }
}

void cmdCycle(Shell &shell, int argc, const ShellArguments &argv) {
  if (argc == 2){
    byte id = byte(atol(argv[1]));
    dev_config[id].state = 1;
    EEPROM.updateBlock<unit>(CONFIG_START_ADDR + sizeof(unit)*id, dev_config[id]);          
    if (dev_config[id].type == 0) {
      actionTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,false);
      delay(1000);
      actionTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,true);
    } else if (dev_config[id].type == 1) {
      kakuTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,false);
      delay(1000);
      kakuTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,true);      
    } else if (dev_config[id].type == 2) {
      blokkerTransmitter.sendSignal(dev_config[id].address,false);
      delay(1000);
      blokkerTransmitter.sendSignal(dev_config[id].address,true);      
    } else if (dev_config[id].type == 3) {
      elroTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,false);
      delay(1000);
      elroTransmitter.sendSignal(dev_config[id].address,dev_config[id].device,true);      
    }
    server.println(F("Unit power cycled"));
  } else {
    server.println(F("Power cycle unit, usage: cycle <id>"));
  }
}

void cmdReset(Shell &shell, int argc, const ShellArguments &argv) {
  for (int i = 0 ; i < EEPROMSizeUno ; i++) {
    EEPROM.write(i, 255);
  }  
  server.println(F("PDU was reseted to factory defaults"));
}

void(* resetFunc) (void) = 0;

void cmdReboot(Shell &shell, int argc, const ShellArguments &argv) {
  resetFunc();
}

void cmdSetnet(Shell &shell, int argc, const ShellArguments &argv) {
  if (argc == 13){
    ip = IPAddress(byte(atol(argv[1])), byte(atol(argv[2])), byte(atol(argv[3])), byte(atol(argv[4])));
    mask = IPAddress(byte(atol(argv[5])), byte(atol(argv[6])), byte(atol(argv[7])), byte(atol(argv[8])));
    gw = IPAddress(byte(atol(argv[9])), byte(atol(argv[10])), byte(atol(argv[11])), byte(atol(argv[12])));
    EEPROM.updateByte(CUSTOM_NETWORK_ADDR, 1);
    EEPROM.updateBlock<IPAddress>(IP_START_ADDR, ip);
    EEPROM.updateBlock<IPAddress>(MASK_START_ADDR, mask);
    EEPROM.updateBlock<IPAddress>(GW_START_ADDR, gw);    
    server.println(F("Network settings updated, reboot device to apply them"));
  } else {
    server.println(F("Set network settings, usage: setnet <4_ip_octets> <4_mask_octets> <4_gateway_octets>"));
  }
}

void cmdIfconfig(Shell &shell, int argc, const ShellArguments &argv) {
  server.print(F("Address: "));
  server.println(ip);
  server.print(F("Netmask: "));
  server.println(mask);
  server.print(F("Gateway: "));
  server.println(gw);
}

ShellCommand(passwd, "Change password, usage: passwd <old_password> <new_password> <repeat_new_password>", cmdPasswd);
ShellCommand(add, "Add unit, usage: add <name> <type(Action=0/KaKu=1/Blokker=2/Elro=3)> <address(0-255)> <device(0-255)>", cmdAdd);
ShellCommand(rm, "Remove unit, usage: rm <id>", cmdRm);
ShellCommand(ls, "List units", cmdLs);
ShellCommand(on, "Power On unit, usage: on <id>", cmdOn);
ShellCommand(off, "Power Off unit, usage: off <id>", cmdOff);
ShellCommand(cycle, "Power cycle unit, usage: cycle <id>", cmdCycle);
ShellCommand(reset, "Reset to factory defaults", cmdReset);
ShellCommand(reboot, "Reboot device", cmdReboot);
ShellCommand(setnet, "Set network settings, usage: setnet <4_ip_octets> <4_mask_octets> <4_gateway_octets>", cmdSetnet);
ShellCommand(ifconfig, "Show current IP settings", cmdIfconfig);

void setup() {
  EEPROM.setMemPool(0, EEPROMSizeUno);
  load_config();
  byte current_config_size = EEPROM.readByte(CONFIG_SIZE_ADDR);
  for (int i = 0; i < current_config_size; i++){
    if (dev_config[i].type == 0) {    
      actionTransmitter.sendSignal(dev_config[i].address,dev_config[i].device,dev_config[i].state);
    } else if (dev_config[i].type == 1) {
      kakuTransmitter.sendSignal(dev_config[i].address,dev_config[i].device,dev_config[i].state);
    } else if (dev_config[i].type == 2) {
      blokkerTransmitter.sendSignal(dev_config[i].address,dev_config[i].state);
    } else if (dev_config[i].type == 3) {
      elroTransmitter.sendSignal(dev_config[i].address,dev_config[i].device,dev_config[i].state);
    }
    delay(500);
  }
  load_network();
  Ethernet.begin(mac, ip, gw, mask);
  server.begin();
  shell.setMachineName("PDU");
  shell.setPasswordCheckFunction(login);
}

void loop() {
  if (!haveClient) {
    // Check for new client connections.
    client = server.available();
    if (client) {
      haveClient = true;
      shell.begin(client, 5);
    }
  } else if (!client.connected()) {
    // The current client has been disconnected.  Shut down the shell.
    shell.end();
    client.stop();
    client = EthernetClient();
    haveClient = false;
  }
  shell.loop();
}


