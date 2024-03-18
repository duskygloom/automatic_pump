#include <WiFi.h>
#include <LiquidCrystal.h>

const uint8_t relay			= 0;
const uint8_t t_top 		= 15;
const uint8_t t_bottom	= 2;
const uint8_t r_bottom	= 4;

const uint8_t d0 = 18;
const uint8_t d1 = 5;
const uint8_t d2 = 17;
const uint8_t d3 = 16;
const uint8_t rs = 21;
const uint8_t en = 19;

WiFiServer server(80, 10);

LiquidCrystal lcd(rs, en, d0, d1, d2, d3);

const String ssid       = "";
const String password   = "";
const String pump_page  = "";

const unsigned long timeout = 2000;

esp_chip_info_t chip_info;
String connectivity = "WiFi";

String header;

TaskHandle_t pump;
TaskHandle_t webserver;

enum PageType {
  BLANK, HOME, DEV
};


/*************************************
 ********** STATE FUNCTIONS **********
 *************************************/

static inline bool is_high(uint8_t pin)
{
  switch (pin) {
    case t_top:
    case t_bottom:
    case r_bottom:
      return digitalRead(pin) == LOW;
    default:
      return digitalRead(pin) == HIGH;
  }
}

/**
 * @return
 * Returns 0 if tank is empty.
 * Returns 1 if tank is full.
 * Returns 2 if tank is half full.
 * Returns 3 if tank is inverted half full.
 */
static inline uint8_t get_tank_state()
{
  if (is_high(t_top) && is_high(t_bottom))
    return 1;
  else if (is_high(t_bottom))
    return 2;
  else if (is_high(t_top))
    return 3;
  return 0;
}

/**
 * @return
 * Returns 0 if reservoir is empty.
 * Returns 1 if reservoir is full.
 */
static inline uint8_t get_reservoir_state()
{
  if (is_high(r_bottom))
    return 1;
  return 0;
}

/**
 * @return
 * Returns 0 if relay is off.
 * Returns 1 if relay is on.
 */
static inline uint8_t get_relay_state()
{
  if (is_high(relay))
    return 1;
  return 0;
}


/***************************
 ********** ICONS **********
 ***************************/

uint8_t tank_empty[]      = { B00000, B01110, B10001, B10001, B10001, B10001, B11111, B00000 };
uint8_t tank_full[]       = { B00000, B01110, B11111, B11111, B11111, B11111, B11111, B00000 };
uint8_t tank_bot_half[]   = { B00000, B01110, B10001, B10001, B11111, B11111, B11111, B00000 };
uint8_t tank_top_half[]   = { B00000, B01110, B11111, B11111, B10001, B10001, B11111, B00000 };
uint8_t pump_off_left[]   = { B00000, B00100, B00111, B01100, B01000, B01100, B00111, B00000 };
uint8_t pump_off_right[]  = { B00000, B00100, B11100, B00110, B00010, B00110, B11100, B00000 };
uint8_t pump_on_left[]    = { B00000, B00100, B00111, B01100, B01001, B01100, B00111, B00000 };
uint8_t pump_on_right[]   = { B00000, B00100, B11100, B00110, B10010, B00110, B11100, B00000 };


/***********************************
 ********** MCU FUNCTIONS **********
 ***********************************/

void setup()
{
	// serial
	Serial.begin(115200);
  while (Serial.available())
    delay(100);
	Serial.println("\n------------------------------------------------------------\n");
  Serial.println("New session");
  // pin setup
	pinMode(relay, OUTPUT);
	pinMode(t_top, INPUT_PULLUP);
	pinMode(t_bottom, INPUT_PULLUP);
	pinMode(r_bottom, INPUT_PULLUP);
	// wifi
	WiFi.begin(ssid, password);
	Serial.println("Connecting...");
	while (WiFi.status() != WL_CONNECTED)
		delay(100);
	Serial.println(String("Connected to ") + WiFi.SSID() + ".");
	Serial.println(String("IP: ") + WiFi.localIP().toString());
  server.begin();
  // lcd
  lcd.begin(16, 2);
  lcd.createChar(0, tank_empty);
  lcd.createChar(1, tank_full);
  lcd.createChar(2, tank_bot_half);
  lcd.createChar(3, tank_top_half);
  lcd.createChar(4, pump_off_left);
  lcd.createChar(5, pump_off_right);
  lcd.createChar(6, pump_on_left);
  lcd.createChar(7, pump_on_right);
  // displaying IP address
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(WiFi.SSID().substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(8000);
  // chip info
  esp_chip_info(&chip_info);
  connectivity += (chip_info.features & CHIP_FEATURE_BT) ? "-BT" : "";
  connectivity += (chip_info.features & CHIP_FEATURE_BLE) ? "-BLE" : "";
  // cores
  xTaskCreatePinnedToCore(handle_pump, "pump", 10000, NULL, 0, &pump, 0);
  xTaskCreatePinnedToCore(handle_client, "webserver", 10000, NULL, 1, &webserver, 1);
}

void loop()
{

}


/************************************
 ********** CORE FUNCTIONS **********
 ************************************/

void handle_pump(void *args)
{
  while (1) {
    if (get_reservoir_state() == 0)
      digitalWrite(relay, LOW);
    else if (get_tank_state() == 0)
      digitalWrite(relay, HIGH);
    else if (get_tank_state() == 1)
      digitalWrite(relay, LOW);
    print_status();
    delay(1000);
  }
}

void handle_client(void *args)
{
  while (1) {
    WiFiClient client = server.available();
    if (client) {
      unsigned long start = millis();
      header = "";
      String line;
      while (millis() - start <= timeout && (line = get_client_line(client)).length() != 0)
        header += line + '\n';
      // dev status json response
      if (header.startsWith(String("GET /") + pump_page + "/dev/status")) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:application/json");
        client.println();
        send_dev_status_json(client);
      }
      // dev page
      else if (header.startsWith(String("GET /") + pump_page + "/dev")) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/html");
        client.println();
        send_page(client, DEV);
      }
      // sensor status json response
      else if (header.startsWith(String("GET /") + pump_page + "/status")) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:application/json");
        client.println();
        send_status_json(client);
      }
      // pump page
      else if (header.startsWith(String("GET /") + pump_page)) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/html");
        client.println();
        send_page(client, HOME);
      }
      // pump control post request
      else if (header.startsWith(String("POST /") + pump_page)) {
        header = "";
        String line;
        unsigned int start = millis();
        while (client.available() && millis() - start <= timeout && (line = get_client_line(client)) != "")
          header += line + '\n';
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/plain");
        client.println();
        if (header.startsWith("OFF") && get_tank_state() != 0) {
          digitalWrite(relay, LOW);
          client.println("OFF");
        }
        else if (header.startsWith("ON") && get_tank_state() != 1 && get_reservoir_state() != 0) {
          digitalWrite(relay, HIGH);
          client.println("ON");
        }
      }
      // not a pump page
      else {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/html");
        client.println();
        send_page(client, BLANK);
      }
      client.stop();
    }
    delay(100);
  }
}


/***************************************
 ********** UTILITY FUNCTIONS **********
 ***************************************/

void print_status()
{
  // this value flips and changes the position of pump icon
  static bool flipflop = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tank      ");
  lcd.write(get_tank_state());
  lcd.print("   ");
  if (flipflop) {
      lcd.write(uint8_t(4+2*get_relay_state()));
      lcd.write(uint8_t(5+2*get_relay_state()));
  }
  lcd.setCursor(0, 1);
  lcd.print("Reservoir ");
  lcd.write(get_reservoir_state());
  lcd.print("   ");
  if (!flipflop) {
      lcd.write(uint8_t(4+2*get_relay_state()));
      lcd.write(uint8_t(5+2*get_relay_state()));
  }
  flipflop = !flipflop;
}

String get_client_line(WiFiClient client)
{
  String line;
  char ch;
  unsigned long start = millis();
  while (client.available() && millis() - start <= timeout && (ch = client.read()) != '\n')
    if (ch != '\r') line += ch;
  return line;
}

/****************************
 ********** EXTRAS **********
 ****************************/

void send_head(WiFiClient client)
{
  client.println("  <head>");
  client.println("    <meta charset='UTF-8'>");
  client.println("    <meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("    <title>Smart pump</title>");
  client.println("    <link rel='icon' type='image/x-icon' href='https://avatars.githubusercontent.com/t/6664638'>");
  client.println("    <link rel='preconnect' href='https://fonts.googleapis.com'>");
  client.println("    <link rel='preconnect' href='https://fonts.gstatic.com' crossorigin>");
  client.println("    <link href='https://fonts.googleapis.com/css2?family=Open+Sans:ital,wght@0,300..800;1,300..800&display=swap' rel='stylesheet'>");
  client.println("    <link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet' integrity='sha384-QWTKZyjpPEjISv5WaRU9OFeRpok6YctnYmDr5pNlyT2bRjXh0JMhjY6hW+ALEwIH' crossorigin='anonymous'>");
  client.println("    <style>");
  client.println("        * { font-family: 'Open Sans' 'Helvetica Neue' Helvetica sans-serif; }");
  client.println("        nav.navbar { color: white; }");
  client.println("        div#main-body { width: 100%; }");
  client.println("    </style>");
  client.println("  </head>");
}

void send_navbar(WiFiClient client)
{
  client.println("    <nav class='navbar shadow bg-primary py-2 text-center'>");
  client.println("      <div class='container-md'>");
  client.println("        <div>");
  client.println("          <img src='https://avatars.githubusercontent.com/t/6664638' alt='iot-club-icon' height='48px'>");
  client.println("        </div>");
  client.println("        <div class='mt-2 pb-0'>");
  client.println("          <h4><b>Automatic Pump</b></h4>");
  client.println("        </div>");
  client.println("        <div>");
  client.println("          <a class='btn btn-primary' onclick='toggle_theme()'>");
  client.println("            <svg id='theme-toggle' xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='white' class='bi bi-circle-half' viewBox='0 0 16 16'>");
  client.println("              <path d='M8 15A7 7 0 1 0 8 1zm0 1A8 8 0 1 1 8 0a8 8 0 0 1 0 16'/>");
  client.println("            </svg>");
  client.println("          </a>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("    </nav>");
}

void send_footer(WiFiClient client)
{
  client.println("    <footer class='text-center pb-2 mt-auto'>");
  client.println("      VBDCSS IoT Club @ 2024");
  client.println("    </footer>");
}


/*****************************
 ********** SCRIPTS **********
 *****************************/

void send_script(WiFiClient client, PageType type)
{
  switch (type) {
  case HOME:
    send_home_script(client);
    break;
  case DEV:
    send_dev_script(client);
    break;
  }
  send_global_script(client);
  client.println("    <script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js' integrity='sha384-YvpcrYf0tY3lHB60NNkmXc5s9fDVZLESaAA55NDzOxhy9GkcIdslK1eN7N6jIeHz' crossorigin='anonymous'></script>");
}

void send_global_script(WiFiClient client)
{
  client.println("    <script>");
  client.println("      function init_theme() {");
  client.println("        theme = localStorage.getItem('theme')");
  client.println("        if (theme) document.documentElement.dataset.bsTheme = theme");
  client.println("      }");
  client.println("      function toggle_theme() {");
  client.println("        dataset = document.documentElement.dataset");
  client.println("        dataset.bsTheme = dataset.bsTheme == 'dark' ? 'light' : 'dark'");
  client.println("        localStorage.setItem('theme', dataset.bsTheme)");
  client.println("      }");
  client.println("    </script>");
}

void send_home_script(WiFiClient client)
{
  client.println("    <script>");
  client.println("      async function send_pump_control() {");
  client.println(String("        await fetch('/" + pump_page + "', {"));
  client.println("          method: 'POST',");
  client.println("          headers: { 'Content-type': 'text/plain' },");
  client.println("          body: document.getElementById('relay_control').innerHTML");
  client.println("        })");
  client.println("      }");
  client.println("    </script>");
  client.println("    <script>");
  client.println("      function refresh_data() {");
  client.println(String("        fetch('/") + pump_page + "/status')");
  client.println("        .then(response => response.json())");
  client.println("        .then(data => {");
  client.println("          for (const key in data) {");
  client.println("            element = document.getElementById(key)");
  client.println("            element.innerHTML = data[key] == 0 ? 'OFF' : 'ON';");
  client.println("            if (key == 'relay')");
  client.println("              document.getElementById('relay_control').innerHTML = data[key] == 1 ? 'OFF' : 'ON'");
  client.println("          }");
  client.println("          if ((data['t_bottom'] == 0 && data['t_top'] == 0) || (data['t_bottom'] == 1 && data['t_top'] == 1) || data['r_bottom'] == 0)");
  client.println("            document.getElementById('relay_control').setAttribute('class', 'btn shadow-sm btn-primary disabled')");
  client.println("          else");
  client.println("            document.getElementById('relay_control').setAttribute('class', 'btn shadow-sm btn-primary')");
  client.println("        })");
  client.println("      }");
  client.println("      setInterval(refresh_data, 1000)");
  client.println("    </script>");
}

void send_dev_script(WiFiClient client)
{
  client.println("  <script>");
  client.println("    function refresh_data() {");
  client.println(String("      fetch('/") + pump_page + "/dev/status')");
  client.println("      .then(response => response.json())");
  client.println("      .then(data => {");
  client.println("        for (const key in data)");
  client.println("          document.getElementById(key).innerHTML = data[key];");
  client.println("      })");
  client.println("    }");
  client.println("    setInterval(refresh_data, 1000)");
  client.println("  </script>");
}


/**************************
 ********** HTML **********
 **************************/

void send_page(WiFiClient client, PageType type)
{
  client.println("<!DOCTYPE html>");
  client.println("<html lang='en' data-bs-theme='dark'>");
  send_head(client);
  client.println("  <body class='d-flex flex-column min-vh-100' onload='init_theme()'>");
  send_navbar(client);
  switch (type) {
  case BLANK:
    send_blank_page(client);
    break;
  case HOME:
    send_home_page(client);
    break;
  case DEV:
    send_dev_page(client);
    break;
  }
  send_footer(client);
  send_script(client, type);
  client.println("  </body>");
  client.println("</html>");
}

void send_blank_page(WiFiClient client)
{
  client.println("    <div id='main-body' class='container-md text-center'>");
  client.println("      <div class='mt-4'>");
  client.println("        <div class='p-4 mt-4'>");
  client.println("          <b class='card-title'>ESP-WROOM-32</b>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("    </div>");
}

void send_home_page(WiFiClient client)
{
  client.println("    <div id='main-body' class='container-md text-center'>");
  client.println("      <div class='card shadow-sm p-4 mt-4'>");
  client.println("        <b class='card-title'>Tank Status</b>");
  client.println("        <div class='card-body'>");
  client.println("          <div class='row'>");
  client.println("            <div class='col'><b>Top</b></div>");
  if (is_high(t_top))
    client.println("            <div class='col' id='t_top'>ON</div>");
  else
    client.println("            <div class='col' id='t_top'>OFF</div>");
  client.println("          </div>");
  client.println("          <div class='row'>");
  client.println("            <div class='col'><b>Bottom</b></div>");
  if (is_high(t_bottom))
    client.println("            <div class='col' id='t_bottom'>ON</div>");
  else
    client.println("            <div class='col' id='t_bottom'>OFF</div>");
  client.println("          </div>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("      <br>");
  client.println("      <div class='card shadow-sm p-4'>");
  client.println("        <b class='card-title'>Reservoir Status</b>");
  client.println("        <div class='card-body'>");
  client.println("          <div class='row'>");
  client.println("            <div class='col'><b>Bottom</b></div>");
  if (is_high(r_bottom))
    client.println("            <div class='col' id='r_bottom'>ON</div>");
  else
    client.println("            <div class='col' id='r_bottom'>OFF</div>");
  client.println("          </div>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("      <br>");
  client.println("      <div class='card shadow-sm p-4'>");
  client.println("        <b class='card-title'>Pump Status</b>");
  client.println("        <div class='card-body'>");
  client.println("          <div class='row'>");
  client.println("            <div class='col'><b>Bottom</b></div>");
  if (is_high(relay))
    client.println("            <div class='col' id='relay'>ON</div>");
  else
    client.println("            <div class='col' id='relay'>OFF</div>");
  client.println("          </div>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("      <br>");
  client.println("      <div class='card shadow-sm p-4'>");
  client.println("        <b class='card-title'>Pump Control</b>");
  client.println("        <div class='card-body row'>");
  if (is_high(relay))
    client.println("          <a class='btn shadow-sm btn-primary' id='relay_control' onclick='send_pump_control()'>OFF</a>");
  else
    client.println("          <a class='btn shadow-sm btn-primary' id='relay_control' onclick='send_pump_control()'>ON</a>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("    </div>");
}

void send_dev_page(WiFiClient client)
{
  client.println("    <div id='main-body' class='container-md text-center'>");
  client.println("      <div class='p-4 mt-4'>");
  client.println("        <b class='card-title'>ESP-WROOM-32</b>");
  client.println("      </div>");
  client.println("    </div>");
  client.println("    <div class='container-md'>");
  client.println("      <div class='p-4 mt-4'>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>Uptime</b></d>");
  client.println(String("          <d class='col'><span id='uptime'>") + millis()/1000 + "</span> s</d>");
  client.println("        </div>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>Free heap</b></d>");
  client.println(String("          <d class='col'><span id='free_heap'>") + ESP.getFreeHeap()/1024 + "</span> KB</d>");
  client.println("        </div>");
  client.println("        <hr>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>Cores</b></d>");
  client.println(String("          <d class='col'><span id='num_cores'>") + chip_info.cores + "</span></d>");
  client.println("        </div>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>Chip model</b></d>");
  client.println(String("          <d class='col'><span id='chip_model'>") + ESP.getChipModel() + "</span></d>");
  client.println("        </div>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>SDK version</b></d>");
  client.println(String("          <d class='col'><span id='sdk_version'>") + ESP.getSdkVersion() + "</span></d>");
  client.println("        </div>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>Connectivity</b></d>");
  client.println(String("          <d class='col'><span id='connectivity'>") + connectivity + "</span></d>");
  client.println("        </div>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>CPU frequency</b></d>");
  client.println(String("          <d class='col'><span id='cpu_frequency'>") + ESP.getCpuFreqMHz() + "</span> MHz</d>");
  client.println("        </div>");
  client.println("        <hr>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>Free sketch space</b></d>");
  client.println(String("          <d class='col'><span id='free_sketch_space'>") + ESP.getFreeSketchSpace()/1024 + "</span> KB</d>");
  client.println("        </div>");
  client.println("        <div class='row'>");
  client.println("          <d class='col'><b>Sketch MD5 checksum</b></d>");
  client.println(String("          <d class='col'><span id='sketch_md5'>") + ESP.getSketchMD5() + "</span></d>");
  client.println("        </div>");
  client.println("      </div>");
  client.println("    </div>");
}

void send_status_json(WiFiClient client)
{
  client.println("{");
  client.println(String("  \"t_top\": ") + is_high(t_top) + ",");
  client.println(String("  \"t_bottom\": ") + is_high(t_bottom) + ",");
  client.println(String("  \"r_bottom\": ") + is_high(r_bottom) + ",");
  client.println(String("  \"relay\": ") + is_high(relay));
  client.println("}");
}

void send_dev_status_json(WiFiClient client)
{
  client.println("{");
  client.println(String("  \"uptime\": ") + millis()/1000 + ",");
  client.println(String("  \"free_heap\": ") + ESP.getFreeHeap()/1024);
  client.println("}");
}
