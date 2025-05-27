#include <Wire.h>         // Biblioteka do komunikacji I2C (dla BME280 i RTC)
#include <SPI.h>          // Biblioteka do komunikacji SPI (dla karty SD i wyświetlacza)
#include <SD.h>           // Biblioteka do obsługi karty SD
#include <Adafruit_Sensor.h> // Podstawowa biblioteka dla czujników Adafruit (wymagana przez BME280)
#include <Adafruit_BME280.h> // Biblioteka do obsługi czujnika temperatury, wilgotności i ciśnienia BME280
#include <RTClib.h>       // Biblioteka do obsługi zegara czasu rzeczywistego (RTC)
#include <Adafruit_GFX.h> // Podstawowa biblioteka graficzna Adafruit (wymagana przez wyświetlacz ST7735)
#include <Adafruit_ST7735.h> // Biblioteka do obsługi wyświetlacza TFT ST7735

#define DARKGREY 0x7BEF     // Definicja koloru ciemnoszarego w formacie 16-bitowym RGB (128,128,128)
//Ciśnienie
#define SEALEVELPRESSURE_HPA (1013.25) // Standardowe ciśnienie na poziomie morza w hektopaskalach (do obliczania wysokości)
// Piny
const int button1Pin = 2;     // Pin cyfrowy podłączony do przycisku 1 (zmienia ekran w lewo/wstecz)
const int button2Pin = 3;     // Pin cyfrowy podłączony do przycisku 2 (zmienia ekran w prawo/dalej)
const int chipSelect = 4;     // Pin Chip Select dla modułu karty SD (ważne dla komunikacji SPI)
const int refreshButtonPin = 7; // Pin cyfrowy podłączony do przycisku odświeżania danych
bool lastRefreshButtonState = HIGH; // Przechowuje poprzedni stan przycisku odświeżania, używane do wykrywania zbocza (naciśnięcia)

// Czujniki i moduły - Deklaracja obiektów dla używanych urządzeń
Adafruit_BME280 bme;        // Obiekt dla czujnika BME280
RTC_PCF8563 rtc;            // Obiekt dla zegara czasu rzeczywistego RTC PCF8563
Adafruit_ST7735 tft = Adafruit_ST7735(10, 9, 8); // Obiekt dla wyświetlacza ST7735: piny (CS, DC, RST)

int screenIndex = 0;        // Aktualnie wyświetlany indeks ekranu (0: bieżące, 1: dziś, 2: wczoraj, 3: tydzień)
const int screenCount = 4;  // Całkowita liczba dostępnych ekranów

int lastLoggedHour = -1;    // Przechowuje ostatnią godzinę, o której zapisano dane na SD, zapobiega wielokrotnemu zapisowi w tej samej godzinie

char csvFileLine[60];       // Bufor do przechowywania linii odczytanej z pliku CSV
File dataFile;              // Obiekt reprezentujący otwarty plik na karcie SD

// Struktura do przechowywania danych z czujnika
struct SensorData {
  float temperature;        // Temperatura w stopniach Celsjusza
  float humidity;           // Wilgotność względna w procentach
  float pressure;           // Ciśnienie atmosferyczne w hektopaskalach
  bool isValid;             // Flaga oznaczająca, czy dane są poprawne/zostały pomyślnie obliczone-
};

// --- Funkcja pomocnicza do centrowania tekstu na wyświetlaczu ---
// text: tekst do wyświetlenia
// y: pozycja pionowa (Y) tekstu na ekranie
// color: kolor tekstu
void drawTextCentered(const char* text, int16_t y, uint16_t color) {
  int16_t x1, y1; // Zmienne pomocnicze dla funkcji getTextBounds
  uint16_t w, h;  // Szerokość i wysokość tekstu
  // Pobierz rozmiar tekstu bez wyświetlania go, aby obliczyć jego wymiary
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (tft.width() - w) / 2; // Oblicz pozycję X dla wyśrodkowania tekstu
  tft.setCursor(x, y);            // Ustaw kursor na wyliczonej pozycji
  tft.setTextColor(color);        // Ustaw kolor tekstu
  tft.print(text);                // Wyświetl tekst
}

// --- Funkcja setup() - Wykonywana raz po uruchomieniu lub zresetowaniu Arduino ---
void setup() {
  Serial.begin(9600);           // Inicjalizacja komunikacji szeregowej z prędkością 9600 bodów/sekundę
  while (!Serial);              // Czekaj, aż monitor szeregowy będzie gotowy (przydatne przy debugowaniu)

  // Konfiguracja pinów przycisków jako wejścia z podciągnięciem do VCC (INPUT_PULLUP)
  // Oznacza to, że pin jest HIGH, gdy przycisk jest niewciśnięty, i LOW, gdy jest wciśnięty.
  pinMode(button1Pin, INPUT_PULLUP);
  pinMode(button2Pin, INPUT_PULLUP);
  pinMode(refreshButtonPin, INPUT_PULLUP);

  // Inicjalizacja czujnika BME280
  if (!bme.begin(0x76)) { // Próba inicjalizacji czujnika pod adresem I2C 0x76 (najczęstszy adres)
    Serial.println("Nie znaleziono BME280!"); // Komunikat o błędzie na monitorze szeregowym
    while (1); // Zatrzymaj program w nieskończonej pętli, jeśli czujnik nie zostanie znaleziony
  }

  // Inicjalizacja modułu RTC
  if (!rtc.begin()) { // Próba inicjalizacji RTC
    Serial.println("RTC PCF8563 nie znaleziono."); // Komunikat o błędzie
    while (1); // Zatrzymaj program, jeśli RTC nie zostanie znaleziony
  }

  // Inicjalizacja karty SD
  if (!SD.begin(chipSelect)) { // Próba inicjalizacji karty SD przy użyciu podanego pinu chipSelect
    Serial.println("Błąd inicjalizacji karty SD"); // Komunikat o błędzie
    // Nie zatrzymujemy programu całkowicie, aby reszta funkcjonalności mogła działać bez SD
  }

  // Otwarcie pliku dane.csv do zapisu (jeśli nie istnieje, zostanie utworzony; jeśli istnieje, zostanie otwarty)
  dataFile = SD.open("dane.csv", FILE_WRITE);
  if (dataFile) { // Sprawdzenie, czy plik został pomyślnie otwarty
    if (dataFile.size() == 0) { // Jeśli plik jest pusty (nowy), dodaj nagłówek
      dataFile.println(F("Date, Time, Temperature, Humidity, Pressure")); // Nagłówek kolumn
    }
    // Plik pozostaje otwarty do dalszych operacji zapisu.
    // Zapewnia to, że strumień zapisu jest gotowy, a plik nie jest za każdym razem otwierany i zamykany,
    // co mogłoby spowolnić działanie i zwiększyć zużycie pamięci.
  } else {
    Serial.println("Nie można otworzyć pliku dane.csv"); // Komunikat o błędzie, jeśli plik nie może być otwarty
  }

  // Inicjalizacja wyświetlacza TFT ST7735
  tft.initR(INITR_BLACKTAB); // Inicjalizacja wyświetlacza z domyślnymi ustawieniami (BLACKTAB jest jednym z typów)
  tft.fillScreen(ST77XX_BLACK); // Wypełnienie całego ekranu kolorem czarnym
  tft.setRotation(1);           // Ustawienie orientacji wyświetlacza (1 to typowo pozioma)
  tft.setTextWrap(false);       // Wyłączenie zawijania tekstu, co daje lepszą kontrolę nad pozycjonowaniem
  tft.setTextColor(ST77XX_WHITE); // Ustawienie domyślnego koloru tekstu na biały
  // tft.setTextSize(2); // Usunięte stąd, aby rozmiar był ustawiany w funkcjach displayXXX dla większej kontroli
  // Rozmiar tekstu jest ustawiany w każdej funkcji wyświetlającej konkretny ekran, aby zapewnić elastyczność.

  Serial.println("Inicjalizacja zakończona.\n"); // Komunikat o zakończeniu inicjalizacji na monitorze szeregowym

  // Wyświetl początkowe dane po uruchomieniu
  drawScreenButtons(screenIndex); // Narysuj przyciski nawigacyjne na dole ekranu
  displayBME280(); // Wywołaj funkcję wyświetlającą bieżące dane z BME280 na ekranie
}

// --- Funkcja loop() - Główna pętla programu, wykonywana wielokrotnie ---
void loop() {
  DateTime now = rtc.now(); // Pobierz aktualny czas i datę z modułu RTC

  // Obsługa przycisków zmiany ekranu
  // Przycisk 1 (button1Pin) - zmiana ekranu w lewo/wstecz
  if (digitalRead(button1Pin) == LOW) { // Sprawdź, czy przycisk został wciśnięty (pin LOW ze względu na pull-up)
    screenIndex--;                      // Zmniejsz indeks ekranu
    if (screenIndex < 0) screenIndex = screenCount - 1; // Jeśli indeks spadnie poniżej 0, przejdź na ostatni ekran
    clearScreen();                      // Wyczyść cały ekran
    drawScreenButtons(screenIndex);     // Narysuj przyciski na dole ekranu, podświetlając aktywny
    // Odśwież zawartość ekranu po zmianie
    if (screenIndex == 0) {
      displayBME280(); // Jeśli to ekran bieżących danych, wyświetl je
    } else {
      updateDisplayForScreenIndex(screenIndex); // W przeciwnym razie zaktualizuj inne ekrany (średnie)
    }
    Serial.print("Przycisk 1 - Ekran: "); // Wyświetl na monitorze szeregowym informację o zmianie ekranu
    Serial.println(screenIndex);
    delay(300); // Opóźnienie (debouncing) aby zapobiec wielokrotnym wykryciom jednego wciśnięcia przycisku
  }

  // Przycisk 2 (button2Pin) - zmiana ekranu w prawo/dalej
  if (digitalRead(button2Pin) == LOW) { // Sprawdź, czy przycisk został wciśnięty
    screenIndex++;                      // Zwiększ indeks ekranu
    if (screenIndex >= screenCount) screenIndex = 0; // Jeśli indeks przekroczy max, wróć na pierwszy ekran (0)
    clearScreen();                      // Wyczyść ekran
    drawScreenButtons(screenIndex);     // Narysuj przyciski
    // Odśwież zawartość ekranu po zmianie
    if (screenIndex == 0) {
      displayBME280(); // Jeśli to ekran bieżących danych, wyświetl je
    } else {
      updateDisplayForScreenIndex(screenIndex); // W przeciwnym razie zaktualizuj inne ekrany
    }
    Serial.print("Przycisk 2 - Ekran: "); // Wyświetl na monitorze szeregowym informację o zmianie ekranu
    Serial.println(screenIndex);
    delay(300); // Opóźnienie (debouncing)
  }
  
  // Zapis danych co godzinę o pełnej godzinie
  // Warunek sprawdza, czy aktualna godzina różni się od ostatnio zapisanej ORAZ czy minuty wynoszą 0.
  if (now.hour() != lastLoggedHour && now.minute() == 0) {
    lastLoggedHour = now.hour(); // Zaktualizuj ostatnio zapisaną godzinę, aby zapobiec ponownemu zapisowi w tej samej godzinie

    SensorData currentReading; // Utwórz zmienną strukturalną do przechowywania bieżących odczytów
    currentReading.temperature = bme.readTemperature(); // Odczyt temperatury
    currentReading.humidity = bme.readHumidity();       // Odczyt wilgotności
    currentReading.pressure = bme.readPressure() / 100.0F; // Odczyt ciśnienia i konwersja na hPa
    currentReading.isValid = true; // Oznacz dane jako poprawne

    saveDatatoSD(currentReading); // Zapisz zebrane dane na kartę SD
  }

  // Obsługa przycisku odświeżania danych
  bool currentRefreshButtonState = digitalRead(refreshButtonPin); // Odczytaj aktualny stan przycisku

  // Wykrycie zbocza opadającego (czyli kliknięcia, bo INPUT_PULLUP oznacza HIGH -> LOW przy wciśnięciu)
  if (lastRefreshButtonState == HIGH && currentRefreshButtonState == LOW) {
    Serial.println("\n--- Odświeżanie danych ---"); // Komunikat na monitorze szeregowym
    // Odświeżanie danych tylko dla ekranu bieżących danych (screenIndex == 0)
    if (screenIndex == 0) {
      displayBME280(); // Wywołaj pełne wyświetlanie, aby uniknąć migotania i odświeżyć dane z czujnika
      Serial.println("Ekran: Bieżące dane");
      // Dane bieżące są już drukowane w displayBME280() na monitorze szeregowym
    } else {
      updateDisplayForScreenIndex(screenIndex); // Odśwież inne ekrany (średnie) po kliknięciu przycisku odświeżania
      Serial.print("Ekran: ");
      switch (screenIndex) { // Wyświetl nazwę aktualnego ekranu na monitorze szeregowym
        case 1: Serial.println("Dzisiaj"); break;
        case 2: Serial.println("Wczoraj"); break;
        case 3: Serial.println("Tydzień"); break;
      }
    }
    Serial.println("------------------------"); // Separator na monitorze szeregowym
    delay(300);     // Opóźnienie (debouncing)
  }
  lastRefreshButtonState = currentRefreshButtonState; // Zapisz aktualny stan przycisku do porównania w następnej iteracji
}

// --- Funkcje pomocnicze ---

// Funkcja do zapisu danych z czujnika na kartę SD
// currentReading: struktura SensorData zawierająca dane do zapisu
void saveDatatoSD(SensorData &currentReading) {
  // Sprawdź, czy plik jest aktualnie otwarty. Jeśli nie, spróbuj go otworzyć ponownie.
  // Jest to zabezpieczenie na wypadek, gdyby plik został przypadkowo zamknięty lub otwarcie w setup() się nie powiodło.
  if (!dataFile) {
    dataFile = SD.open("dane.csv", FILE_WRITE); // Spróbuj ponownie otworzyć plik w trybie zapisu
    if (!dataFile) { // Jeśli ponowne otwarcie również się nie powiedzie
      Serial.println("Błąd otwarcia pliku dane.csv do zapisu"); // Zgłoś błąd
      return; // Zakończ funkcję, nie można zapisać danych
    }
  }

  DateTime now = rtc.now(); // Pobierz aktualny czas i datę z RTC

  // Formatowanie i zapis danych do pliku CSV
  // Przykład formatu: RRRR-MM-DD, HH:MM:SS, Temperatura, Wilgotność, Ciśnienie
  dataFile.print(now.year(), DEC); // Rok
  dataFile.print(F("-"));
  if (now.month() < 10) dataFile.print('0'); // Dodaj wiodące zero dla miesiąców < 10
  dataFile.print(now.month(), DEC);          // Miesiąc
  dataFile.print(F("-"));
  if (now.day() < 10) dataFile.print('0');   // Dodaj wiodące zero dla dni < 10
  dataFile.print(now.day(), DEC);            // Dzień
  dataFile.print(F(", "));                   // Separator
  if (now.hour() < 10) dataFile.print('0');  // Dodaj wiodące zero dla godzin < 10
  dataFile.print(now.hour(), DEC);           // Godzina
  dataFile.print(F(":"));
  if (now.minute() < 10) dataFile.print('0'); // Dodaj wiodące zero dla minut < 10
  dataFile.print(now.minute(), DEC);         // Minuty
  dataFile.print(F(":"));
  if (now.second() < 10) dataFile.print('0'); // Dodaj wiodące zero dla sekund < 10
  dataFile.print(now.second(), DEC);         // Sekundy
  dataFile.print(F(", "));                   // Separator
  dataFile.print(currentReading.temperature, 2); // Temperatura z 2 miejscami po przecinku
  dataFile.print(F(", "));                   // Separator
  dataFile.print(currentReading.humidity, 2);    // Wilgotność z 2 miejscami po przecinku
  dataFile.print(F(", "));                   // Separator
  dataFile.println(currentReading.pressure, 2);  // Ciśnienie z 2 miejscami po przecinku i znak nowej linii

  dataFile.flush(); // Wymuś zapis danych z bufora na kartę SD (ważne, aby dane nie zostały utracone przy odłączeniu zasilania)

  Serial.println("Zapisano dane na SD."); // Komunikat potwierdzający zapis
}

// Funkcja do rysowania przycisków nawigacyjnych na dole ekranu
// activeIndex: indeks aktualnie aktywnego ekranu, który zostanie podświetlony
void drawScreenButtons(int activeIndex) {
  int buttonWidth = tft.width() / screenCount; // Oblicz szerokość każdego przycisku, dzieląc szerokość ekranu przez liczbę ekranów
  int buttonHeight = 20;                     // Wysokość przycisku w pikselach
  int y = tft.height() - buttonHeight;       // Pozycja Y (pionowa) dla przycisków (na dole ekranu)

  for (int i = 0; i < screenCount; i++) { // Pętla dla każdego przycisku
    uint16_t color = (i == activeIndex) ? ST77XX_YELLOW : DARKGREY; // Jeśli to aktywny przycisk, ustaw żółty kolor, w przeciwnym razie ciemnoszary
    tft.fillRect(i * buttonWidth, y, buttonWidth, buttonHeight, color); // Narysuj prostokąt przycisku
    tft.setCursor(i * buttonWidth + 2, y + 6); // Ustaw kursor dla tekstu wewnątrz przycisku (małe wcięcie)
    tft.setTextColor(ST77XX_BLACK);           // Ustaw kolor tekstu na czarny
    tft.setTextSize(1);                       // Ustaw rozmiar tekstu na 1 (mała czcionka, stała dla przycisków)

    switch (i) { // Wyświetl skrót nazwy ekranu na przycisku
      case 0: tft.print("Bie"); break;  // Bieżące
      case 1: tft.print("Dzis"); break; // Dzisiaj
      case 2: tft.print("Wcz"); break;  // Wczoraj
      case 3: tft.print("Tyg"); break;  // Tydzień
    }
  }
}

// Funkcja do czyszczenia całego ekranu wyświetlacza
void clearScreen() {
  tft.fillScreen(ST77XX_BLACK); // Wypełnij cały ekran kolorem czarnym
  tft.setCursor(0, 0);          // Ustaw kursor w lewym górnym rogu
  tft.setTextColor(ST77XX_WHITE); // Ustaw domyślny kolor tekstu na biały
  tft.setTextSize(1);           // Ustaw domyślny rozmiar tekstu na 1 (będzie nadpisany w funkcjach wyświetlających dane)
}

// Funkcja do wyświetlania bieżących danych z czujnika BME280
void displayBME280() {
  clearScreen();              // Wyczyść ekran
  drawScreenButtons(screenIndex); // Narysuj przyciski nawigacyjne na dole

  // Nagłówek ekranu (mniejsza czcionka, centrowany)
  tft.setTextSize(1);               // Ustaw rozmiar tekstu dla nagłówka
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK); // Ustaw kolor tekstu (biały) i tła (czarny) dla nagłówka
  drawTextCentered("Biezace dane:", 10, ST77XX_WHITE); // Wyśrodkuj i wyświetl nagłówek

  // Odczyt danych z czujnika BME280
  float temp = bme.readTemperature();
  float pressure = bme.readPressure() / 100.0F; // Konwersja Pa na hPa
  float altitude = bme.readAltitude(SEALEVELPRESSURE_HPA); // Obliczenie wysokości na podstawie ciśnienia i ciśnienia na poziomie morza
  float humidity = bme.readHumidity();

  // Wyświetlanie danych na monitorze szeregowym (do debugowania)
  Serial.println("--- Bieżące dane z BME280 ---");
  Serial.print("Temp: "); Serial.print(temp); Serial.println(" C");
  Serial.print("Cisn: "); Serial.print(pressure); Serial.println(" hPa");
  Serial.print("Wysokosc: "); Serial.print(altitude); Serial.println(" m");
  Serial.print("Wilgotnosc: "); Serial.print(humidity); Serial.println(" %");

  // Sprawdzenie, czy odczytane dane nie są niepoprawne (NaN - Not a Number)
  if (isnan(temp) || isnan(pressure) || isnan(altitude) || isnan(humidity)) {
    drawTextCentered("Blad czujnika!", 40, ST77XX_RED); // Wyświetl komunikat o błędzie na ekranie
    Serial.println("Błąd: Dane z czujnika są niepoprawne (NaN)."); // Zgłoś błąd na monitorze szeregowym
    return; // Zakończ funkcję, aby nie wyświetlać błędnych danych
  }

  // Ustawianie kursora i rozmiaru tekstu dla wyświetlanych danych
  int indentX = 10; // Wcięcie od lewej krawędzi dla estetyki
  int startY = 30;  // Początkowa pozycja Y dla pierwszej linii danych

  tft.setTextSize(1); // Ustaw rozmiar tekstu na większy dla danych (można zmienić na 2 dla większego tekstu)
  tft.setTextColor(ST77XX_WHITE); // Ustaw kolor tekstu na biały

  // Wyświetlanie danych na wyświetlaczu TFT
  tft.setCursor(indentX, startY);
  tft.print("Temp: "); tft.print(temp, 2); tft.println(" C"); // Temperatura z 2 miejscami po przecinku

  tft.setCursor(indentX, tft.getCursorY()); // Ustaw kursor na następnej linii
  tft.print("Cisn: "); tft.print(pressure, 2); tft.println(" hPa"); // Ciśnienie

  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Wysokosc: "); tft.print(altitude, 2); tft.println(" m"); // Wysokość

  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Wilg: "); tft.print(humidity, 2); tft.println(" %"); // Wilgotność (skrócono "Wilg" dla spójności)
}

// Funkcja do aktualizacji zawartości wyświetlacza w zależności od aktywnego indeksu ekranu
// index: indeks ekranu do wyświetlenia
void updateDisplayForScreenIndex(int index) {
  clearScreen();              // Wyczyść ekran
  drawScreenButtons(index);   // Narysuj przyciski nawigacyjne

  // Komentarze usunięte z oryginału, ponieważ ustawienia tekstu są specyficzne dla każdej funkcji displayXXX
  // tft.setTextSize(1);
  // tft.setTextColor(ST77XX_WHITE);

  switch (index) { // Wywołaj odpowiednią funkcję wyświetlającą dla danego indeksu ekranu
    case 0:
      displayBME280();      // Ekran bieżących danych
      break;
    case 1:
      displayTodayAvg();    // Ekran średnich danych z dzisiaj
      break;
    case 2:
      displayYesterdayAvg(); // Ekran średnich danych z wczoraj
      break;
    case 3:
      displayWeekAvg();     // Ekran średnich danych z ostatniego tygodnia
      break;
  }
}

// Funkcja do wyświetlania średnich danych z dzisiaj
void displayTodayAvg() {
  clearScreen();              // Wyczyść ekran
  drawScreenButtons(screenIndex); // Narysuj przyciski nawigacyjne

  // Nagłówek ekranu
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  drawTextCentered("Dzis - srednia", 10, ST77XX_WHITE); // Wyśrodkuj i wyświetl nagłówek

  SensorData avg = calculateAverageFromCSV(0); // Oblicz średnie dane z dzisiaj (0 dni wstecz)

  Serial.println("--- Średnie dane z dzisiaj ---"); // Komunikat na monitorze szeregowym
  // Ustawianie kursora i rozmiaru tekstu dla danych
  int indentX = 10;
  int startY = 30;

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(indentX, startY);
  if (!avg.isValid) { // Sprawdź, czy średnie dane są poprawne (czy były jakieś dane do obliczeń)
    tft.setTextColor(ST77XX_RED); // Zmień kolor tekstu na czerwony dla komunikatu o błędzie
    tft.println("Brak danych z dzisiaj!"); // Wyświetl komunikat o braku danych
    Serial.println("Brak danych z dzisiaj!");
    return; // Zakończ funkcję
  }
  // Wyświetlanie średnich danych na monitorze szeregowym
  Serial.print("Temp: "); Serial.print(avg.temperature, 2); Serial.println(" C");
  Serial.print("Cisn: "); Serial.print(avg.pressure, 2); Serial.println(" hPa");
  Serial.print("Wilg: "); Serial.print(avg.humidity, 2); Serial.println(" %");

  // Wyświetlanie średnich danych na wyświetlaczu TFT
  tft.print("Temp: "); tft.print(avg.temperature, 2); tft.println(" C");
  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Cisn: "); tft.print(avg.pressure, 2); tft.println(" hPa");
  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Wilg: "); tft.print(avg.humidity, 2); tft.println(" %");
}

// Funkcja do wyświetlania średnich danych z wczoraj
void displayYesterdayAvg() {
  clearScreen();
  drawScreenButtons(screenIndex);

  // Nagłówek ekranu
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  drawTextCentered("Wczoraj - srednia", 10, ST77XX_WHITE);

  SensorData avg = calculateAverageFromCSV(1); // Oblicz średnie dane z wczoraj (1 dzień wstecz)

  Serial.println("--- Średnie dane z wczoraj ---");
  // Ustawianie kursora i rozmiaru tekstu dla danych
  int indentX = 10;
  int startY = 30;

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(indentX, startY);
  if (!avg.isValid) {
    tft.setTextColor(ST77XX_RED);
    tft.println("Brak danych z wczoraj!");
    Serial.println("Brak danych z wczoraj!");
    return;
  }
  Serial.print("Temp: "); Serial.print(avg.temperature, 2); Serial.println(" C");
  Serial.print("Cisn: "); Serial.print(avg.pressure, 2); Serial.println(" hPa");
  Serial.print("Wilg: "); Serial.print(avg.humidity, 2); Serial.println(" %");

  tft.print("Temp: "); tft.print(avg.temperature, 2); tft.println(" C");
  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Cisn: "); tft.print(avg.pressure, 2); tft.println(" hPa");
  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Wilg: "); tft.print(avg.humidity, 2); tft.println(" %");
}

// Funkcja do wyświetlania średnich danych z ostatniego tygodnia
void displayWeekAvg() {
  clearScreen();
  drawScreenButtons(screenIndex);

  // Nagłówek ekranu
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  drawTextCentered("Tydzien - srednia", 10, ST77XX_WHITE);

  SensorData avg = calculateWeeklyAverage(); // Oblicz średnie dane z tygodnia

  Serial.println("--- Średnie dane z tygodnia ---");
  // Ustawianie kursora i rozmiaru tekstu dla danych
  int indentX = 10;
  int startY = 30;

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(indentX, startY);
  if (!avg.isValid) {
    tft.setTextColor(ST77XX_RED);
    tft.println("Brak danych z ostatniego tygodnia!");
    Serial.println("Brak danych z ostatniego tygodnia!");
    return;
  }
  Serial.print("Temp: "); Serial.print(avg.temperature, 2); Serial.println(" C");
  Serial.print("Cisn: "); Serial.print(avg.pressure, 2); Serial.println(" hPa");
  Serial.print("Wilg: "); Serial.print(avg.humidity, 2); Serial.println(" %");


  tft.print("Temp: "); tft.print(avg.temperature, 2); tft.println(" C");
  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Cisn: "); tft.print(avg.pressure, 2); tft.println(" hPa");
  tft.setCursor(indentX, tft.getCursorY());
  tft.print("Wilg: "); tft.print(avg.humidity, 2); tft.println(" %");
}

// --- Funkcje do wyliczania średnich z pliku CSV ---

// Funkcja obliczająca średnie wartości temperatury, wilgotności i ciśnienia
// dla danych z określonej liczby dni wstecz.
// daysBack: 0 dla dzisiaj, 1 dla wczoraj, itd.
SensorData calculateAverageFromCSV(int daysBack) {
  File file = SD.open("dane.csv"); // Otwórz plik danych CSV do odczytu
  SensorData sum = {0, 0, 0, false}; // Inicjalizacja sumatorów i flagi isValid na false
  int count = 0; // Licznik poprawnych odczytów

  if (file) { // Sprawdź, czy plik został pomyślnie otwarty
    DateTime now = rtc.now(); // Pobierz aktualną datę z RTC

    // Pomijamy pierwszą linię nagłówkową pliku CSV
    file.readBytesUntil('\n', csvFileLine, sizeof(csvFileLine) - 1);

    while (file.available()) { // Dopóki są dostępne dane w pliku
      // Odczytaj jedną linię z pliku CSV
      int len = file.readBytesUntil('\n', csvFileLine, sizeof(csvFileLine) - 1);
      csvFileLine[len] = '\0'; // Dodaj znak null na końcu odczytanej linii, aby była poprawnym stringiem C
       
      // Parsowanie daty (format: YYYY-MM-DD) z początku linii CSV
      int y = atoi(csvFileLine);      // Rok
      int m = atoi(csvFileLine + 5);  // Miesiąc (przesunięcie o 5 znaków: YYYY-)
      int d = atoi(csvFileLine + 8);  // Dzień (przesunięcie o 8 znaków: YYYY-MM-)

      DateTime recordDate(y, m, d); // Utwórz obiekt DateTime dla daty zapisu z pliku
      TimeSpan delta = now - recordDate; // Oblicz różnicę czasową między aktualną datą a datą rekordu

      // Sprawdź, czy rekord pochodzi z wybranego dnia (np. dzisiaj, wczoraj)
      if (delta.days() == daysBack) {
        // Znajdź początek danych pomiarowych po dacie i czasie
        char *ptr = strchr(csvFileLine, ',') + 1; // Przejdź za pierwszą przecinek (po dacie)
        ptr = strchr(ptr, ',') + 1;             // Przejdź za drugi przecinek (po czasie)

        // Konwersja stringów na wartości zmiennoprzecinkowe (float)
        float temp = atof(ptr);                 // Temperatura
        ptr = strchr(ptr, ',') + 1;             // Przejdź za kolejny przecinek

        float hum = atof(ptr);                  // Wilgotność
        ptr = strchr(ptr, ',') + 1;             // Przejdź za kolejny przecinek

        float press = atof(ptr);                // Ciśnienie

        // Dodatkowe sprawdzenie, czy odczytane wartości nie są absurdalne
        // (np. bardzo duże liczby z powodu błędnego parsowania lub uszkodzenia danych)
        // Możesz dostosować te zakresy do swoich potrzeb, aby odfiltrować nieprawidłowe dane.
        if (!isnan(temp) && !isnan(hum) && !isnan(press) && // Upewnij się, że nie są to NaN
            temp > -50 && temp < 100 &&                 // Przykładowy realny zakres temperatur
            hum >= 0 && hum <= 100 &&                   // Przykładowy realny zakres wilgotności
            press > 500 && press < 1200) {              // Przykładowy realny zakres ciśnienia
          sum.temperature += temp; // Dodaj do sumy
          sum.humidity += hum;
          sum.pressure += press;
          count++;                 // Zwiększ licznik poprawnych odczytów
        } else {
          Serial.print("Ostrzeżenie: pominięto niepoprawne dane z pliku SD (dzień ");
          Serial.print(daysBack); Serial.print("): ");
          Serial.println(csvFileLine); // Wypisz ostrzeżenie o pominiętej linii
        }
      }
    }
    file.close(); // Zamknij plik po zakończeniu odczytu
  }

  if (count > 0) { // Jeśli były jakieś poprawne dane do obliczenia średniej
    sum.temperature = sum.temperature / count; // Oblicz średnią temperaturę
    sum.humidity = sum.humidity / count;       // Oblicz średnią wilgotność
    sum.pressure = sum.pressure / count;       // Oblicz średnie ciśnienie
    sum.isValid = true; // Oznacz dane jako ważne, ponieważ były pomiary
  } else {
    // Jeśli nie ma danych dla danego dnia, ustaw wartości na NaN (Not a Number)
    // i oznacz isValid jako false, aby wskazać brak poprawnych danych.
    sum.temperature = NAN;
    sum.humidity = NAN;
    sum.pressure = NAN;
    sum.isValid = false;
  }
  return sum; // Zwróć strukturę ze średnimi danymi lub NaN
}

// Funkcja obliczająca średnie wartości z całego ostatniego tygodnia (7 dni)
SensorData calculateWeeklyAverage() {
  SensorData sum = {0, 0, 0, false}; // Inicjalizacja sumatorów i flagi isValid na false
  int validDays = 0;                 // Licznik dni, dla których znaleziono poprawne dane

  // Pętla od dzisiaj (0 dni wstecz) do 6 dni wstecz, łącznie 7 dni
  for (int i = 0; i < 7; i++) {
    SensorData day = calculateAverageFromCSV(i); // Oblicz średnie dla każdego dnia
    if (day.isValid) { // Jeśli dane dla danego dnia są poprawne (były pomiary)
      sum.temperature += day.temperature; // Dodaj średnie dzienne do sumy tygodniowej
      sum.humidity += day.humidity;
      sum.pressure += day.pressure;
      validDays++; // Zwiększ licznik ważnych dni
    }
  }

  if (validDays > 0) { // Jeśli znaleziono jakiekolwiek ważne dni z danymi
    sum.temperature /= validDays; // Oblicz średnią tygodniową temperaturę
    sum.humidity /= validDays;    // Oblicz średnią tygodniową wilgotność
    sum.pressure /= validDays;    // Oblicz średnie tygodniowe ciśnienie
    sum.isValid = true; // Oznacz średnią tygodniową jako ważną
  } else {
    // Jeśli nie ma żadnych ważnych danych z ostatnich 7 dni, ustaw wartości na NaN
    // i oznacz isValid jako false.
    sum.temperature = NAN;
    sum.humidity = NAN;
    sum.pressure = NAN;
    sum.isValid = false;
  }
  return sum; // Zwróć strukturę ze średnimi tygodniowymi danymi lub NaN
}
