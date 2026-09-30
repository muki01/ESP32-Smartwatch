# Security Policy

## Supported versions

Security fixes are made for the latest release on the `main` branch.

## Reporting a vulnerability

Please do **not** open a public issue for security problems. Send the details to **muksin.muksin04@gmail.com** instead: what is affected, how to reproduce it and the possible impact. You will get an answer within a few days.

## Things to know

- **Wireless update:** the update server only runs while you turn it on in Settings › System › Update, and each upload needs the 6-digit PIN shown on the watch. Turn it off again after updating.
- **Stored data:** Wi-Fi passwords and settings are stored in the ESP32's NVS flash partition, which is not encrypted by default.
- **Bluetooth:** the phone link uses the Nordic UART service of the Bangle.js protocol; pair only with devices you trust.
