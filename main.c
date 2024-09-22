/*
Created by: 
    - DAOUDI MOHAMMED
Check the full project documentation in my blog : 
7emoutyX.github.io
*/

#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <libpq-fe.h>

#define LCD_CHR 1 // Mode - Sending data
#define LCD_CMD 0 // Mode - Sending command
#define LINE1 0x80 // Address of the 1st line
#define LINE2 0xC0 // Address of the 2nd line
#define LCD_BACKLIGHT 0x08  // On
#define ENABLE 0b00000100   // Enable bit
#define SENSOR1_PIN 27 // Infrared sensor 1
#define SENSOR2_PIN 22 // Infrared sensor 2
#define DEBOUNCE_DELAY 200 // Debounce delay in milliseconds
#define DB_CONNINFO "host=localhost dbname=newfuji user=postgres password=bobafett"
#define BUZZER_PIN 18
#define LED_PIN 10
#define SENSOR_DISTANCE 0.07 // in meters
#define SPACE_BETWEEN_OBJECTS 0.2 // in meters
#define OBJECT_LENGTH 5.0 // in meters


void play_melody();
void play_tone();
void play_tone2();
int lcd;
int objectCount = 0;
char direction[10] = "FIX";
float speed = 0.0;
time_t last_detection_time = 0;
time_t last_speed_update_time = 0;

void lcd_toggle_enable(int bits) {
    usleep(500);
    bits |= ENABLE;
    wiringPiI2CWrite(lcd, bits);
    usleep(500);
    bits &= ~ENABLE;
    wiringPiI2CWrite(lcd, bits);
    usleep(500);
}

void lcd_byte(int bits, int mode) {
    wiringPiI2CWrite(lcd, mode | (bits & 0xF0) | LCD_BACKLIGHT);
    lcd_toggle_enable(mode | (bits & 0xF0) | LCD_BACKLIGHT);

    wiringPiI2CWrite(lcd, mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT);
    lcd_toggle_enable(mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT);
}

void lcd_init() {
    lcd_byte(0x33, LCD_CMD);
    lcd_byte(0x32, LCD_CMD);
    lcd_byte(0x06, LCD_CMD);
    lcd_byte(0x0C, LCD_CMD);
    lcd_byte(0x28, LCD_CMD);
    lcd_byte(0x01, LCD_CMD);
    usleep(500);
}

void lcd_send_string(const char *str) {
    while (*str) {
        lcd_byte(*(str++), LCD_CHR);
    }
}

void display_info() {
    char buffer1[16];
    char buffer2[16];

    snprintf(buffer1, sizeof(buffer1), "Count: %d", objectCount);
    snprintf(buffer2, sizeof(buffer2), "Dir: %s %.1f/h", direction, speed);

    lcd_byte(0x01, LCD_CMD);
    usleep(500);

    lcd_byte(LINE1, LCD_CMD);
    lcd_send_string(buffer1);
    usleep(1000);

    lcd_byte(LINE2, LCD_CMD);
    lcd_send_string(buffer2);
    usleep(1000);
}
void calculate_speed() {
    time_t current_time = time(NULL);

    if (last_detection_time != 0) {
        double elapsed_seconds = difftime(current_time, last_detection_time);
        double distance_covered = OBJECT_LENGTH + SPACE_BETWEEN_OBJECTS;
        double speed_mps = distance_covered / elapsed_seconds;
        speed = speed_mps * 3.6; // Convert to km/h
    }
    last_detection_time = current_time;
    last_speed_update_time = current_time;
}

const char* determine_shift(struct tm *current_time) {
    if (current_time->tm_hour >= 6 && current_time->tm_hour < 14) {
        return "Morning";
    } else if (current_time->tm_hour >= 14 && current_time->tm_hour < 22) {
        return "Evening";
    } else {
        return "Night";
    }
}

void send_to_database(int count, float speed, const char *direction, const char *shift) {
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO passed_objects (count, name, speed, passed_hour, shift_id, passed_date) "
             "VALUES (%d, '%s', %f, EXTRACT(HOUR FROM CURRENT_TIMESTAMP), "
             "(SELECT id FROM shifts WHERE name = '%s'), CURRENT_DATE);",
             count, direction, speed, shift);
    PGconn *conn = PQconnectdb(DB_CONNINFO);
    if (PQstatus(conn) == CONNECTION_OK) {
        PGresult *res = PQexec(conn, query);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "INSERT failed: %s\n", PQerrorMessage(conn));
        } else {
            printf("Data inserted successfully: %s\n", query);
        }
        PQclear(res);
    } else {
        fprintf(stderr, "Database connection failed: %s", PQerrorMessage(conn));
    }
    PQfinish(conn);
}

int get_current_day_count(PGconn *conn) {
    int count = 0;
    PGresult *res = PQexec(conn,
        "SELECT COUNT(*) FROM passed_objects "
        "WHERE passed_date = CURRENT_DATE;");
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        count = atoi(PQgetvalue(res, 0, 0));
    } else {
        fprintf(stderr, "SELECT failed: %s\n", PQerrorMessage(conn));
    }
    PQclear(res);
    return count;
}

int get_last_object_count(PGconn *conn) {
    const char *query = "SELECT COUNT(*) FROM passed_objects WHERE passed_date = CURRENT_DATE";
    PGresult *res = PQexec(conn, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "SELECT failed: %s", PQerrorMessage(conn));
        PQclear(res);
        return 0;
    }

    int count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    return count;
}

void play_tone(int duration, int frequency) {
    int half_period = 1000000 / frequency / 2;
    int cycles = frequency * duration / 1000;

    for (int i = 0; i < cycles; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        digitalWrite(LED_PIN, HIGH);
        delayMicroseconds(half_period);
        digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(LED_PIN, LOW);
        delayMicroseconds(half_period);
    }
}

void welcome_fujikura() {
    char buffer1[16];
    char buffer2[16];

    snprintf(buffer1, sizeof(buffer1), "    FUJIKURA    ");
    snprintf(buffer2, sizeof(buffer2), "  AUTOMOTIVE K1 ");

    lcd_byte(0x01, LCD_CMD);
    usleep(500);
    lcd_byte(LINE1, LCD_CMD);
    lcd_send_string(buffer1);
    usleep(500);
    lcd_byte(LINE2, LCD_CMD);
    lcd_send_string(buffer2);
    usleep(500);
    play_melody();
}

void play_melody() {
    play_tone(200, 523); // C5
    delay(100);
    play_tone(200, 659); // E5
    delay(100);
    play_tone(200, 784); // G5
    delay(100);
    play_tone(400, 1047); // C6
}

void play_tone2(int duration) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    delay(duration);
}

int main() {
    wiringPiSetupGpio();
    lcd = wiringPiI2CSetup(0x27); // Use your LCD I2C address
    lcd_init();
    welcome_fujikura();

    pinMode(SENSOR1_PIN, INPUT);
    pinMode(SENSOR2_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);

    int sensor1_state, sensor2_state;
    int prev_sensor1_state = HIGH;
    int prev_sensor2_state = HIGH;

    PGconn *conn = PQconnectdb(DB_CONNINFO);
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return 1;
    }
    objectCount = get_last_object_count(conn);
    objectCount = get_current_day_count(conn);

    char count_str[16];
    snprintf(count_str, sizeof(count_str), "Count: %d", objectCount);
    lcd_send_string(count_str);
    display_info();

    while (1) {
        sensor1_state = digitalRead(SENSOR1_PIN);
        sensor2_state = digitalRead(SENSOR2_PIN);

        time_t current_time = time(NULL);

        if (sensor1_state == LOW && prev_sensor1_state == HIGH) {
            delay(DEBOUNCE_DELAY);
            if (digitalRead(SENSOR2_PIN) == LOW) {
                objectCount++;
                strcpy(direction, "CW");
                calculate_speed();

                time_t now = time(NULL);
                struct tm *current_tm = localtime(&now);
                const char *shift = determine_shift(current_tm);

                display_info();
                send_to_database(objectCount, speed, direction, shift);

                play_tone2(200);
            }
        }
        if (sensor2_state == LOW && prev_sensor2_state == HIGH) {
            delay(DEBOUNCE_DELAY);
            if (digitalRead(SENSOR1_PIN) == LOW) {
                objectCount++;
                strcpy(direction, "CCW");
                calculate_speed();

                time_t now = time(NULL);
                struct tm *current_tm = localtime(&now);
                const char *shift = determine_shift(current_tm);

                display_info();
                send_to_database(objectCount, speed, direction, shift);

                play_tone2(200);
            }
        }
        // Check if 10 seconds have passed since the last speed update
        if (difftime(current_time, last_speed_update_time) >= 10) {
            speed = 0.0;
            display_info();

            // Send speed = 0 to the database without incrementing the object count
            time_t now = time(NULL);
            struct tm *current_tm = localtime(&now);
            const char *shift = determine_shift(current_tm);
            send_to_database(objectCount, speed, "NONE", shift);

            last_speed_update_time = current_time; // Reset the timer
        }
    
        prev_sensor1_state = sensor1_state;
        prev_sensor2_state = sensor2_state;
        delay(10);
        }
    PQfinish(conn);
    return 0;
}