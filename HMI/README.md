# What This Folder Contain

This "HMI" Folder is used for build and compile "C code" in HMI Display and This also 1 Type of MicroController

# After Clone From Git What to do in this HMI Folder

Step 01 : Verify "ExpressIf" Extenstion Already Installed If not Install 1st Install it.

step 02 : Down Left Side Expressif(ESP-IDF) Icons Show 

            --> 1st Check Inside "HMI Folder " Show
            --> 2nd Check "ESP-IDF v5.5.6" Version Selected in "Select Current ESP-IDFVersion ICON" If Not Selected Select Them.
            --> 3rd Check "UART" Selected in "Select Flash Method ICON" if Not Selected Select Them.
            --> 4th Check  PORT Selected in "Select port to use ICON" if not Select (eg; COM3 or COM4 or COM5)
            --> 5th Check  IN "set EspressIf Device target ICON" Seclect in "esp32s3" if not selected select them.

Step 03 : After all Complete must Activate ESP-IDF "& "C:\esp\v5.5.6\esp-idf\export.ps1""

step 04: After Activate ESP-IDF Verify is it Activate "python "$env:IDF_PATH\tools\idf.py" --version"
            
step 05 : After Checking Click Delete Icon for "Full Clean"

Step 06 : After Full Clean Click build ICON for "Converting Human readable Code to Binary Code"

Step 07 : After Build Click Flash Icon for "Sending Binary Code to ESP32 S3"

Step 08 : After Flash Click Monitor Device ICON for Viewing Logs in Terminal.

Step 09 : If Any Error Show Copy and Paste that Error in ChatGPT or Antigravity(RECOMMENDED).