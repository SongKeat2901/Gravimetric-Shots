// Minimal serial test - just blink and print
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n\n");
  Serial.println("=================================");
  Serial.println("MINIMAL SERIAL TEST");
  Serial.println("ESP32-S3 is alive!");
  Serial.println("=================================");
  Serial.flush();
}

void loop() {
  static int counter = 0;
  Serial.printf("Loop %d - millis: %lu\n", counter++, millis());
  delay(1000);
}
