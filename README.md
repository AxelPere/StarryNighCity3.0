# Starry Night City 3.0 (Pico 2W 3D Renderer)

A custom real-time 3D polygon-projected graphics engine and cyberpunk city simulation running entirely on a **Raspberry Pi Pico 2W (RP2350)** microcontroller, displayed on a 4-inch ST7796 TFT screen.

---

## Features
* **Custom 3D Graphics Pipeline:** Full 3D perspective projection, rotation matrices (`rotate_x`, `rotate_y`), and backface culling implemented from scratch in C.
* **Procedural City Generation:** Dynamically generates skyscrapers with randomized heights, architectural depths, and glowing window patterns that scale towards a dense city center.
* **Atmospheric Elements:** Procedural night sky with twinkling starfields, a glowing moon with crater rim shading, and a custom flat island ground grid.
* **Simulated Traffic & Infrastructure:** Looping autonomous traffic channels with randomized car body colors and headlights, paired with glowing street lamps.
* **High-Performance Framebuffer:** Utilizes a full 300KB RAM framebuffer blasted over high-speed SPI (`65 MHz`) for buttery-smooth rendering without tearing.

---

## Hardware Requirements
* **Microcontroller:** Raspberry Pi Pico 2W (RP2350)
* **Display:** 4" TFT ST7796 SPI Display (480x320 resolution)
* **Power:** Powered via VBUS (5V) for optimal display brightness and backlight regulation.

---

## Wiring Diagram (Pinout)

| Pico 2W Pin | ST7796 Pin |
| :--- | :--- |
| **GPIO 17** | CS |
| **GPIO 20** | DC | 
| **GPIO 21** | RST | 
| **GPIO 19** | MOSI (SPI0 TX) 
| **GPIO 18** | SCK (SPI0 Clock) 
| **GPIO 22** | LED
| **VBUS (5V)** | VCC
| **GND** | GND
