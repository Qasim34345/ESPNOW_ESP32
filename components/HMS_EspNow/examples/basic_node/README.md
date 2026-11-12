# Basic node example

This standalone project builds the repository-root application so the code you test and edit is visible under `main/`.

- `main/app_main.cpp`: forwarding source for the root app.
- `main/demo_modes.h` and `main/peer_table.h`: forwarding headers for the root board settings.
- Root `main/app_main.cpp`: startup, callbacks and periodic `Hello! World` TX.

The default settings target the second ESP32-S3 as RX. To run the first board as TX, change the role and remote peer MAC in the root `main/` settings.

NVS startup intentionally stops on recovery conditions rather than erasing the partition. Define an application-specific migration or explicit erase policy before adapting the example for a product.

Build standalone from this directory or build the repository root. Select the target explicitly, for example `idf.py set-target esp32s3`.
