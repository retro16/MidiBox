#ifndef _MENU_H
#define _MENU_H

#include <itoa.h>
#include <LiquidCrystal_I2C_STM32.h>
#include <SD.h>

struct MenuItem {
  MenuItem(const char *name_): name(name_), parent(NULL) {}
  virtual const char * line1() {
    return name;
  }
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual const char * line2() = 0;
  virtual MenuItem * onKeyPressed(int keys) = 0;
  const char *name;
  MenuItem *parent = 0;

protected:

  static char displayBuffer[17];
};

struct MenuList: public MenuItem {
  template<typename... SubItems>
  MenuList(const char *name_, MenuItem &subItem_, SubItems&... subItems_): MenuItem(name_), curItem(0) {
    addSubItems(0, subItem_, subItems_...);
  }

  virtual void onEnter() {
    curItem = 0;
  }

  virtual const char * line2();
  virtual MenuItem * onKeyPressed(int keys);

private:
  void addSubItems(int) {}
  
  template<typename... SubItems>
  void addSubItems(int position, MenuItem &subItem, SubItems&... subItems_) {
    subItem.parent = this;
    subItems[position] = &subItem;
    subItems[position + 1] = NULL;
    addSubItems(position + 1, subItems_...);
  }

  MenuItem *subItems[8];
  int curItem;
};

struct Menu {
  static const int KEY_UP = 1 << 0;
  static const int KEY_DOWN = 1 << 1;
  static const int KEY_LEFT = 1 << 2;
  static const int KEY_RIGHT = 1 << 3;

  Menu(MenuItem &mainMenu_, int upKeyPin_, int downKeyPin_, int leftKeyPin_, int rightKeyPin_):
    currentMenu(&mainMenu_),
    mainMenu(&mainMenu_),
    upKeyPin(upKeyPin_),
    downKeyPin(downKeyPin_),
    leftKeyPin(leftKeyPin_),
    rightKeyPin(rightKeyPin_),
    lastKeyState(0),
    lcd(0x27, 16, 2)
  {}

  void init() {
    pinMode(upKeyPin, INPUT_PULLUP);
    pinMode(downKeyPin, INPUT_PULLUP);
    pinMode(leftKeyPin, INPUT_PULLUP);
    pinMode(rightKeyPin, INPUT_PULLUP);
    lcd.begin();
    lcd.noCursor();
    currentMenu->onEnter();
    displayRefresh();
    wallClock = millis() + REFRESH_PERIOD;
  }

  // Call in the main loop
  void poll() {
    // Read pressed keys
    readKeys();
  }

  void switchTo(MenuItem &newMenu) {
    if(currentMenu != &newMenu)
      newMenu.onEnter();
    currentMenu = &newMenu;
  }

  void switchToMain() {
    while(currentMenu != mainMenu) {
      currentMenu->onExit();
      currentMenu = currentMenu->parent;
    }
    displayRefresh();
  }

private:
  static const int REFRESH_PERIOD = 50; // Screen refresh period in milliseconds
  static const int KEY_REPEAT_FIRST = 12; // Refresh periods before starting key repeat
  static const int KEY_REPEAT_NEXT = 4; // Refresh periods between 2 key repeats
  static const int KEY_REPEAT_ACCEL_THRES = 5; // Repeats before going to fast mode
  static const int KEY_REPEAT_FAST = 1; // Refresh periods between 2 key repeats

  void onKeyPressed(int keys);
  void readKeys();
  void displayRefresh();

  int upKeyPin;
  int downKeyPin;
  int leftKeyPin;
  int rightKeyPin;
  int lastKeyState; // Pressed keys
  int keyRepeat; // Frame counter for key repeat
  int keyRepeatCount;
  int wallClock;
  LiquidCrystal_I2C_STM32 lcd;
  MenuItem *currentMenu;
  MenuItem *mainMenu;
};

struct MenuNumberSelect: public MenuItem {
  MenuNumberSelect(const char *name_, MenuItem &subMenu_, int minimum_, int maximum_):
    MenuItem(name_),
    subMenu(subMenu_),
    minimum(minimum_),
    maximum(maximum_) {
    if(&subMenu != this)
      subMenu.parent = this;
  }
  virtual void onEnter() {
    number = minimum;
  }
  virtual const char * line2() {
    itoa(number, displayBuffer, 10);
    return displayBuffer;
  }
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_LEFT && number > minimum)
      --number;
    else if(keys == Menu::KEY_RIGHT && number < maximum)
      ++number;
    else if(keys == Menu::KEY_DOWN)
      return &subMenu;
    return this;
  }
  MenuItem &subMenu;
  int minimum;
  int maximum;
  int number;
};

struct MenuConfirm: public MenuItem {
  MenuConfirm(const char *name_, const char *message_):
    MenuItem(name_),
    message(message_)
  {}

  MenuConfirm(const char *name_, const char *message_, MenuItem& parent_):
    MenuItem(name_),
    message(message_)
  {
    parent = &parent_;
  }

  virtual const char * line2() {
    return message;
  }

  virtual MenuItem * onKeyPressed(int keys);
  virtual void onConfirmed() {}

  const char *message;
};

struct MenuFileSelect: public MenuItem {
  MenuFileSelect(const char *name_, MenuItem &subMenu_, int csPin_, const char *path_):
    MenuItem(name_),
    csPin(csPin_),
    path(path_),
    subMenu(subMenu_) {
    if(&subMenu != this)
      subMenu.parent = this;
  }

  virtual void onEnter() {
    // Clear any selection and reinit the SD card each time the menu is entered
    // That allows for more robust operation when hot-switching cards
    if(SD.begin(csPin)) {
      dir = SD.open(path);
      if(!dir) {
        SD.mkdir(path);
        dir = SD.open(path);
      }
    }
    fileIndex = 0;
    fileCount = -1; // Number of files unknown
    openFileAtIndex();
  }

  void onExit() {
    if(file)
      file.close();
    if(dir)
      dir.close();
    SD.end();
  }

  virtual const char * line2() {
    if(!dir) {
      return "SD CARD ERROR";
    }

    if(!file) {
      return "NO FILE";
    }

    return file.name();
  }

  void openFileAtIndex() {
    if(!dir)
      return;
    dir.rewindDirectory();
    for(int i = 0; i <= fileIndex; ++i) {
      openNextFile();
    }
  }

  void openNextFile() {
    if(!dir)
      return;
    do {
      if(file)
        file.close();
      file = dir.openNextFile();
    } while(file && file.isDirectory());
  }

  virtual MenuItem * onKeyPressed(int keys) {
    if(!dir)
      // Not ready, don't react to any keypress
      return this;

    if(keys == Menu::KEY_LEFT && fileIndex > 0) {
      --fileIndex;
      openFileAtIndex();
    } else if(keys == Menu::KEY_RIGHT) {
      if(fileIndex < fileCount - 1 || fileCount == -1) {
        ++fileIndex;
        openNextFile();
      }
      if(fileCount == -1) {
        if(!file) {
          fileCount = fileIndex;
          --fileIndex;
          openFileAtIndex();
        }
      }
    }

    // Only allow entering submenu if a file was selected
    else if(keys == Menu::KEY_DOWN && file) {
      // Go to the beginning of the file (in case the same file is selected more than once in a row)
      file.seek(0);
      return &subMenu;
    }

    return this;
  }

  MenuItem &subMenu;
  int csPin;
  const char *path;
  int fileIndex;
  int fileCount;
  File dir;
  File file;
};


#endif
