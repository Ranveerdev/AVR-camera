// 12-Pixel LDR Camera with Cubic Upscaling to 10x10
// Digital pins for OUTPUT, Analog pins only for INPUT

const int numPixels = 12;
int RawArray[12] = {0};
int UpscaledArray[100] = {0}; // 10x10 = 100 pixels

// Digital pins for OUTPUT, Analog pins for INPUT
const int outputPins[8] = {2, 3, 4, 5, 6, 7, 8, 9};  // Digital outputs (D2-D9)
const int inputPins[6] = {A0, A1, A2, A3, A4, A5};   // Analog inputs (A0-A5)

// Pixel mapping: {outputPin, inputPin} for 12 pixels (3x4 grid)
const int pixelPins[12][2] = {
  // Row 0: 4 pixels
  {2, A0}, {4, A2}, {6, A4}, {8, A0},  // Pixels 0,1,2,3
  // Row 1: 4 pixels  
  {3, A1}, {5, A3}, {7, A5}, {9, A4},  // Pixels 4,5,6,7
  // Row 2: 4 pixels
  {10, A1}, {11, A2}, {12, A3}, {13, A4}   // Pixels 8,9,10,11
};

void setup() {
  Serial.begin(9600);
  
  // Initialize all digital pins as INPUT (off) initially
  for (int i = 2; i <= 13; i++) {
    pinMode(i, INPUT);
    digitalWrite(i, LOW);
  }
  
  Serial.println("12-Pixel LDR Camera - 3x4 to 10x10 Upscaling");
  Serial.println("Digital Outputs: D2-D13");
  Serial.println("Analog Inputs: A0-A5");
}

int readPixel(int pixel) {
  int outPin = pixelPins[pixel][0];  // Digital output pin
  int inPin = pixelPins[pixel][1];   // Analog input pin
  
  // Turn off all output pins first
  for (int i = 2; i <= 13; i++) {
    pinMode(i, INPUT);
    digitalWrite(i, LOW);
  }
  
  // Turn on ONLY the current output pin
  pinMode(outPin, OUTPUT);
  digitalWrite(outPin, HIGH);
  
  delay(20); // Settling time
  
  int value = analogRead(inPin);
  
  // Turn off the output pin
  pinMode(outPin, INPUT);
  digitalWrite(outPin, LOW);
  
  return value;
}

// Debug function to test each pixel individually
void debugPixels() {
  Serial.println("=== DEBUG: Testing Each Pixel ===");
  
  for (int pixel = 0; pixel < numPixels; pixel++) {
    int outPin = pixelPins[pixel][0];
    int inPin = pixelPins[pixel][1];
    
    Serial.print("Pixel ");
    Serial.print(pixel);
    Serial.print(" (Row ");
    Serial.print(pixel / 4);
    Serial.print(", Col ");
    Serial.print(pixel % 4);
    Serial.print("): D");
    Serial.print(outPin);
    Serial.print(" -> A");
    Serial.print(inPin - A0);
    Serial.print(" : ");
    
    int value = readPixel(pixel);
    Serial.println(value);
    
    delay(500);
  }
  Serial.println("=== DEBUG COMPLETE ===");
}

// Cubic interpolation function
float cubicInterpolate(float p[4], float x) {
  return p[1] + 0.5 * x * (p[2] - p[0] + 
         x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] + 
         x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

// Bicubic upscaling from 3x4 to 10x10
void cubicUpscale3x4to10x10(int input[12], int output[100]) {
  float input2D[3][4];  // 3 rows, 4 columns
  float output2D[10][10];
  
  // Convert 1D to 2D (3 rows x 4 columns)
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 4; x++) {
      input2D[y][x] = input[y * 4 + x];
    }
  }
  
  // Bicubic interpolation from 3x4 to 10x10
  for (int y = 0; y < 10; y++) {
    for (int x = 0; x < 10; x++) {
      float xMap = (x * 3.0 / 9.0); // Map 0-9 to 0-3
      float yMap = (y * 2.0 / 9.0); // Map 0-9 to 0-2
      
      int x1 = floor(xMap);
      int y1 = floor(yMap);
      float xFrac = xMap - x1;
      float yFrac = yMap - y1;
      
      x1 = constrain(x1, 0, 3);
      y1 = constrain(y1, 0, 2);
      
      // Get 4x4 neighborhood for bicubic
      float arr[4][4];
      for (int j = -1; j <= 2; j++) {
        for (int i = -1; i <= 2; i++) {
          int xi = constrain(x1 + i, 0, 3);
          int yj = constrain(y1 + j, 0, 2);
          arr[j+1][i+1] = input2D[yj][xi];
        }
      }
      
      // Apply bicubic interpolation
      float interpolated[4];
      for (int j = 0; j < 4; j++) {
        float column[4] = {arr[j][0], arr[j][1], arr[j][2], arr[j][3]};
        interpolated[j] = cubicInterpolate(column, xFrac);
      }
      
      output2D[y][x] = cubicInterpolate(interpolated, yFrac);
      output2D[y][x] = constrain(output2D[y][x], 0, 1023);
    }
  }
  
  // Convert back to 1D
  for (int y = 0; y < 10; y++) {
    for (int x = 0; x < 10; x++) {
      output[y * 10 + x] = (int)output2D[y][x];
    }
  }
}

// TEMPORARY: Increase contrast in code
void applyContrastEnhancement() {
  // Find min and max values
  int minVal = 1024, maxVal = 0;
  for(int i = 0; i < 12; i++) {
    if(RawArray[i] < minVal) minVal = RawArray[i];
    if(RawArray[i] > maxVal) maxVal = RawArray[i];
  }
  
  // Apply contrast stretch
  for(int i = 0; i < 12; i++) {
    RawArray[i] = map(RawArray[i], minVal, maxVal, 0, 1023);
  }
}

void captureImage() {
  Serial.println("Capturing 12-pixel image...");
  
  for (int pixel = 0; pixel < numPixels; pixel++) {
    RawArray[pixel] = readPixel(pixel);
  applyContrastEnhancement();
    
    // Print each pixel as it's captured for debugging
    Serial.print("P");
    Serial.print(pixel);
    Serial.print(":");
    Serial.print(RawArray[pixel]);
    Serial.print(" ");
    
    if ((pixel + 1) % 4 == 0) Serial.println(); // New line every 4 pixels
    
    delay(50);
  }
  Serial.println();
}

void displayOriginal() {
  Serial.println("Original 3x4 Image:");
  Serial.println("===================");
  
  for (int y = 0; y < 3; y++) {
    Serial.print("| ");
    for (int x = 0; x < 4; x++) {
      int pixelIndex = y * 4 + x;
      int val = RawArray[pixelIndex];
      char ch;
      
      if (val > 800) ch = ' ';
      else if (val > 600) ch = '.';
      else if (val > 400) ch = '*';
      else if (val > 200) ch = 'o';
      else ch = '#';
      
      Serial.print(ch);
      Serial.print(" ");
    }
    Serial.println("|");
  }
  Serial.println("===================");
}

void displayUpscaled() {
  Serial.println("Upscaled 10x10 Image (Cubic Interpolation):");
  Serial.println("===========================================");
  
  for (int y = 0; y < 10; y++) {
    Serial.print("| ");
    for (int x = 0; x < 10; x++) {
      int val = UpscaledArray[y * 10 + x];
      char ch;
      
      if (val > 800) ch = ' ';
      else if (val > 700) ch = '.';
      else if (val > 600) ch = ':';
      else if (val > 500) ch = '=';
      else if (val > 400) ch = '+';
      else if (val > 300) ch = 'o';
      else if (val > 200) ch = 'O';
      else if (val > 100) ch = '0';
      else ch = '@';
      
      Serial.print(ch);
    }
    Serial.println(" |");
  }
  Serial.println("===========================================");
}

void printValues() {
  Serial.println("Raw Values (3x4):");
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 4; x++) {
      int pixelIndex = y * 4 + x;
      Serial.print("P");
      Serial.print(pixelIndex);
      Serial.print(":");
      Serial.print(RawArray[pixelIndex]);
      Serial.print("\t");
    }
    Serial.println();
  }
}

// ADD THIS FUNCTION FOR IMAGE SAVING
// IMPROVED image data saving function
// IMPROVED image data saving function
void saveImageData() {
  Serial.println("IMAGE_DATA_START");
  delay(50);
  
  // Save raw array (3x4)
  Serial.println("RAW_3x4");
  for (int i = 0; i < 12; i++) {
    Serial.println(RawArray[i]);
    delay(5); // Small delay to prevent buffer overflow
  }
  Serial.println("RAW_END");
  delay(50);
  
  // Save upscaled array (10x10)  
  Serial.println("UPSCALED_10x10");
  for (int i = 0; i < 100; i++) {
    Serial.println(UpscaledArray[i]);
    delay(5); // Small delay to prevent buffer overflow
  }
  Serial.println("UPSCALED_END");
  delay(50);
  
  Serial.println("IMAGE_DATA_END");
  Serial.flush(); // Ensure all data is sent
}

void loop() {
  Serial.println("\n==================================================");
  Serial.println("CHOOSE MODE:");
  Serial.println("1 - Debug (test each pixel individually)");
  Serial.println("2 - Capture image with upscaling");
  Serial.println("Enter choice (1 or 2):");
  
  // Wait for user input
  while (!Serial.available()) {
    delay(100);
  }
  
  char choice = Serial.read();
  
  if (choice == '1') {
    debugPixels();
  } else {
    Serial.println("READY - Show an alphabet/object to the camera!");
    Serial.println("Image will auto-capture in 3 seconds...");
    delay(3000);
    
    // Capture image
    captureImage();
    
    // Display original image
    displayOriginal();
    
    // Apply cubic upscaling
    cubicUpscale3x4to10x10(RawArray, UpscaledArray);
    
    // Display upscaled image
    displayUpscaled();
    
    // Show numerical values
    printValues();
    
    // ADD THIS: Save image data for Python
    saveImageData();
    
    Serial.println("Image data sent to Python! Check for .bmp files.");
  }
  
  Serial.println("Restarting in 5 seconds...");
  delay(5000);
}