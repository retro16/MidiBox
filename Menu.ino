#include "Menu.h"

char MenuItem::displayBuffer[17];

const char * MenuList::line2() {
  return subItems[curItem]->name;
}

MenuItem * MenuList::onKeyPressed(int keys) {
  if(curItem > 0 && (keys == Menu::KEY_LEFT)) {
    --curItem;
    return this;
  } else if (subItems[curItem + 1] && (keys == Menu::KEY_RIGHT)) {
    ++curItem;
    return this;
  } else if(keys == Menu::KEY_DOWN) {
    return subItems[curItem];
  }
  return NULL;
}

void Menu::displayRefresh() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(currentMenu->line1());
  lcd.setCursor(0, 1);
  lcd.print(currentMenu->line2());
}

void Menu::onKeyPressed(int keys) {
  if(keys == Menu::KEY_UP && currentMenu->parent && currentMenu->parent != currentMenu) {
    currentMenu->onExit();
    currentMenu = currentMenu->parent;
  } else {
    MenuItem *nextMenu = currentMenu->onKeyPressed(keys);
    if(nextMenu == currentMenu->parent) {
      currentMenu->onExit();
      currentMenu = currentMenu->parent;
    } else if(nextMenu && nextMenu != currentMenu) {
      currentMenu = nextMenu;
      currentMenu->onEnter();
    }
  }
  displayRefresh();
}

MenuItem * MenuConfirm::onKeyPressed(int keys) {
  if(keys == Menu::KEY_DOWN) {
    onConfirmed();
    return parent;
  }
  return NULL;
}

void Menu::readKeys() {
  if(!currentMenu)
    return;

  // Read keys in a fashion that returns only one pressed key at a time
  int keys = 0;
  keys = digitalRead(upKeyPin) ? 0 : KEY_UP;
  keys = !keys ? (digitalRead(downKeyPin) ? 0 : KEY_DOWN) : keys;
  keys = !keys ? (digitalRead(leftKeyPin) ? 0 : KEY_LEFT) : keys;
  keys = !keys ? (digitalRead(rightKeyPin) ? 0 : KEY_RIGHT) : keys;
  if(keys) {
    if(keys != lastKeyState) {
      keyRepeat = KEY_REPEAT_FIRST;
      keyRepeatCount = 0;
      onKeyPressed(keys);
    } else {
      // Key hold
      if(!--keyRepeat) {
        ++keyRepeatCount;
        if(keyRepeatCount >= KEY_REPEAT_ACCEL_THRES)
          keyRepeat = KEY_REPEAT_FAST;
        else
          keyRepeat = KEY_REPEAT_NEXT;
        onKeyPressed(keys);
      }
    }
  }
  lastKeyState = keys;
}
