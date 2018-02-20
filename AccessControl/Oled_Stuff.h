
// uncomment next line to enable OLED on WEMOS stuff
//#define USE_OLED_WEMOS


#ifdef USE_OLED_WEMOS

void oled_clock_update() { 
  // OLED DISPLAY Check if we need to update seconds, minutes, hours:
  if (lastDraw + CLOCK_SPEED < millis())
  {
    lastDraw = millis();
    // Add a second, update minutes/hours if necessary:
    updateTime();
    
    // Draw the clock:
    oled.clear(PAGE);  // Clear the buffer
    drawFace();  // Draw the face to the buffer
    drawArms(hours, minutes, seconds);  // Draw arms to the buffer
    oled.display(); // Draw the memory buffer
  }
}

void initClockVariables()
{
  // Calculate constants for clock face component positions:
  oled.setFontType(0);
  CLOCK_RADIUS = _min(MIDDLE_X, MIDDLE_Y) - 1;
  POS_12_X = MIDDLE_X - oled.getFontWidth();
  POS_12_Y = MIDDLE_Y - CLOCK_RADIUS + 2;
  POS_3_X  = MIDDLE_X + CLOCK_RADIUS - oled.getFontWidth() - 1;
  POS_3_Y  = MIDDLE_Y - oled.getFontHeight()/2;
  POS_6_X  = MIDDLE_X - oled.getFontWidth()/2;
  POS_6_Y  = MIDDLE_Y + CLOCK_RADIUS - oled.getFontHeight() - 1;
  POS_9_X  = MIDDLE_X - CLOCK_RADIUS + oled.getFontWidth() - 2;
  POS_9_Y  = MIDDLE_Y - oled.getFontHeight()/2;
  
  // Calculate clock arm lengths
  S_LENGTH = CLOCK_RADIUS - 2;
  M_LENGTH = S_LENGTH * 0.7;
  H_LENGTH = S_LENGTH * 0.5;
}

// Simple function to increment seconds and then increment minutes
// and hours if necessary.
void updateTime()
{
  seconds++;  // Increment seconds
  if (seconds >= 60)  // If seconds overflows (>=60)
  {
    seconds = 0;  // Set seconds back to 0
    minutes++;    // Increment minutes
    if (minutes >= 60)  // If minutes overflows (>=60)
    {
      minutes = 0;  // Set minutes back to 0
      hours++;      // Increment hours
      if (hours >= 12)  // If hours overflows (>=12)
      {
        hours = 0;  // Set hours back to 0
      }
    }
  }
}

// Draw the clock's three arms: seconds, minutes, hours.
void drawArms(int h, int m, int s)
{
  double midHours;  // this will be used to slightly adjust the hour hand
  static int hx, hy, mx, my, sx, sy;
  
  // Adjust time to shift display 90 degrees ccw
  // this will turn the clock the same direction as text:
  h -= 3;
  m -= 15;
  s -= 15;
  if (h <= 0)
    h += 12;
  if (m < 0)
    m += 60;
  if (s < 0)
    s += 60;
  
  // Calculate and draw new lines:
  s = map(s, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  sx = S_LENGTH * cos(PI * ((float)s) / 180);  // woo trig!
  sy = S_LENGTH * sin(PI * ((float)s) / 180);  // woo trig!
  // draw the second hand:
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + sx, MIDDLE_Y + sy);
  
  m = map(m, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  mx = M_LENGTH * cos(PI * ((float)m) / 180);  // woo trig!
  my = M_LENGTH * sin(PI * ((float)m) / 180);  // woo trig!
  // draw the minute hand
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + mx, MIDDLE_Y + my);
  
  midHours = minutes/12;  // midHours is used to set the hours hand to middling levels between whole hours
  h *= 5;  // Get hours and midhours to the same scale
  h += midHours;  // add hours and midhours
  h = map(h, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  hx = H_LENGTH * cos(PI * ((float)h) / 180);  // woo trig!
  hy = H_LENGTH * sin(PI * ((float)h) / 180);  // woo trig!
  // draw the hour hand:
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + hx, MIDDLE_Y + hy);
}

// Draw an analog clock face
void drawFace()
{
  // Draw the clock border
  oled.circle(MIDDLE_X, MIDDLE_Y, CLOCK_RADIUS);
  
  // Draw the clock numbers
  oled.setFontType(0); // set font type 0, please see declaration in SFE_MicroOLED.cpp
  oled.setCursor(POS_12_X, POS_12_Y); // points cursor to x=27 y=0
  oled.print(12);
  oled.setCursor(POS_6_X, POS_6_Y);
  oled.print(6);
  oled.setCursor(POS_9_X, POS_9_Y);
  oled.print(9);
  oled.setCursor(POS_3_X, POS_3_Y);
  oled.print(3);
}

#endif
