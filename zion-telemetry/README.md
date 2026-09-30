# Zion Racing — Telemetry System

TESC500 (VESC) → CAN → ESP32-S3 ECU → 4G → Firebase Realtime Database → web dashboard (one screen for pit wall, laptop and phone).

```
public/index.html                      Dashboard website (live + demo mode)
firmware/zion_telemetry_ecu/           ESP32-S3 firmware (edit config.h only)
database.rules.json                    Firebase security rules
firebase.json                          Firebase Hosting + rules deploy config
```

## 1. Wiring

| From | To | Notes |
|---|---|---|
| TESC CAN pin 2 CAN_H (orange) | Transceiver CANH | Shielded twisted pair, shield grounded at ECU end only |
| TESC CAN pin 3 CAN_L (brown) | Transceiver CANL | |
| TESC CAN pin 4 GND (black) | ECU GND | Common ground is required |
| TESC CAN pin 1 5V (red) | **leave unconnected** | Power the ECU from the 12 V buck, not the controller |
| Transceiver TXD / RXD | GPIO 4 / GPIO 5 | Use a 3.3 V-logic transceiver: TCAN1042V, TJA1051T/3 or SN65HVD230 |
| SIM7600 RXD / TXD / PWRKEY | GPIO 17 / 18 / 21 | Modem needs its own ≥2 A supply peak; add 1000 µF near the module |
| GPS TX / RX | GPIO 8 / 9 | 9600 baud default (u-blox M8/M10, ATGM336H) |
| microSD CS / MOSI / SCK / MISO | GPIO 10 / 11 / 12 / 13 | FAT32 card |
| Wheel hall sensor output | GPIO 6 | Open-collector (A3144/US5881); pull-up to **3.3 V**, never 5 V |
| Lap sensor output | GPIO 7 | IR beacon receiver / magnetic strip sensor; level-shift if 5–12 V |
| Battery NTC 10k (B3950) | GPIO 1 | 3.3 V — 10 kΩ — GPIO1 — NTC — GND; tape NTC to a cell |

CAN termination: measure CANH–CANL with power off. You want ≈60 Ω (two 120 Ω terminators, one at each end of the bus). If you read ≈120 Ω, enable the terminator on the ECU transceiver board.

Power: 12 V aux → fuse → TVS (SMBJ18A) + reverse-polarity diode/P-MOSFET → 12→5 V buck (≥3 A) → ESP32-S3 5 V pin + SIM7600 board.

## 2. VESC Tool settings (TESC500)

1. App Settings → General → CAN Mode: **VESC**, CAN Baud Rate: **CAN_BAUD_500K**. Note the **Controller ID**.
2. App Settings → General → CAN Status Message Rate: 50 Hz, and enable **Status 1, 2, 4, 5** (in VESC fw 6.x: "CAN Status Messages Rate 1" with CAN_STATUS_1_2_3_4_5).
3. Motor Settings → General → Temp: set the motor sensor to **NTC 100K** (the TAFM uses a 100K NTC).
4. Write the app configuration. Read "Motor Poles" and put poles ÷ 2 into `MOTOR_POLE_PAIRS`.

## 3. Firebase

1. Create a Firebase project → Build → **Realtime Database** → Create database (Singapore / asia-southeast1). Copy its URL.
2. Rules tab: paste `database.rules.json` and publish. Nothing can be read without signing in; the ECU writes with the secret.
3. Build → **Authentication** → Get started → Sign-in method → enable **Email/Password**.
4. Authentication → Users → **Add user**: one team account, e.g. `pit@zionracing.team` with the team password.
   The email doesn't need to be a real inbox.
5. Authentication → Settings → User actions → **untick "Enable create (sign-up)"**, so nobody can make their own account.
6. Project settings → General → copy the **Web API key** (it's safe in the web page; the rules protect the data).
7. Project settings → Service accounts → Database secrets → copy the secret into `FIREBASE_SECRET` in the firmware only.

To change the password later: Authentication → Users → ⋮ → Reset password (or delete and re-add the user).
Every device then has to sign in again the next time its login refreshes (within an hour).

## 4. Firmware

1. Arduino IDE 2 + ESP32 core 3.x. Board: *ESP32S3 Dev Module*, USB CDC On Boot: Enabled.
2. Library Manager: install **TinyGSM** and **TinyGPSPlus**.
3. Edit `config.h` (lines marked `<-- CHANGE`): database URL, secret, APN, pole pairs, gear ratio, wheel diameter, pack cells/capacity.
4. Flash and open Serial Monitor at 115200. You should see `[CAN] 500 kbit/s started`, then a status line every 2 s with `can ok`.

On boot the ECU reads `race/` once and continues the lap numbering, so a restart never breaks a running session. The microSD keeps a 10 Hz CSV of everything (`/zion_001.csv`, `/zion_002.csv`, …) even when 4G drops.

## 5. Dashboard on the college website

1. Open `public/index.html` in a text editor. In the `SITE` block near the top of the `<script>`, fill in
   `firebaseUrl`, `apiKey` and `loginEmail` (the team account from step 3.4).
2. Upload `index.html` to its own folder on the college server, e.g. `https://college.edu/zion/`. It is the only file needed.
3. Open the URL. You'll see the sign-in screen; enter the team password. The device stays signed in until someone taps **Sign out**.

### Sessions

- **Start session** (header) names the session and starts counting laps from the next start/finish crossing.
- While a session runs, every signed-in screen shows it. **Sign out** and **Data source** are locked,
  and closing the tab asks for confirmation.
- **End session** needs the team password (checked by Firebase). The session's laps are saved to
  `zion/kart01/sessions/<id>` and the live lap list is cleared for the next session.
- Turning the kart off and on during a session is fine: the ECU continues the lap numbering.

Useful URLs: `…/zion/?demo` (simulated kart), `…/zion/?reset` (reset this device's settings).

### Track map

The map shows the COASST Karting Track, Coimbatore, with turns T1–T12. In live mode it draws the kart's GPS trace; once the kart completes a full lap, the turn numbers are placed along that lap by distance from start/finish. This assumes the lap sensor sits at the start/finish line and the kart runs the normal direction (start/finish → T1 → … → T12).

With `firebaseUrl` or `apiKey` empty the page runs in preview mode: demo kart, password `demo`.

## 6. Troubleshooting

| Symptom | Check |
|---|---|
| `can SILENT` | CAN baud/mode in VESC Tool, H/L swapped, missing common ground, termination |
| Speed wrong | `WHEEL_DIAMETER_M`, `WHEEL_PULSES_PER_REV`; with sensor off, `MOTOR_POLE_PAIRS` and `GEAR_RATIO` |
| `upload failed (-1…-4)` | APN, SIM data balance, modem supply sagging during transmit |
| `upload failed (401)` | `FIREBASE_SECRET` wrong |
| Dashboard "Offline" / "Kart offline" | `firebaseUrl` in index.html, rules published, ECU shows `[4G] online` |
| "Sign-in isn't set up yet" | `apiKey` in index.html, Email/Password sign-in enabled |
| `race sync failed` in Serial Monitor | Database URL/secret in config.h; laps are held and uploaded once it works |
| SOC jumps | Set `PACK_SERIES_CELLS` / `PACK_CAPACITY_AH`; SOC is re-estimated only at low load |
