
// Термореле с управлением по СМС. 
// V2_0 Добавил библиотеку таймеров
// Добавлена прповерка по таймеру связи модуля SIM800 и связи с модулем,если связи нет - перезагрузка
// V2_1 Добавлено хранение в нестираемой памяти EEPROM состояние термореле и заданную температуру
// V2_2 Перенастройка выводов управления под печатную плату
// V2_3 Переработан механизм обработки СМС, чтобы не висло на спам сообщениях с русскими буквами. Стабильная версия (проработало6 месяцев без перезагрузок)
// V2-4 Переработка SMS информирования 


#include <LiquidCrystal_I2C.h>      // подключаем библиотеку дисплея,подключение к Ардуино SDA – A4, SCL – A5
LiquidCrystal_I2C lcd(0x27, 16, 2); // задаем формат дисплея - адрес, столбцов, строк

#include <microDS18B20.h>           // подключаем библиотеку термо датчиков DS18B20
MicroDS18B20<2> sensor1;            // указываем пин подключения 1 датчика, 4,7 кОм подтянуть к питанию
MicroDS18B20<3> sensor2;            // указываем пин подключения 2 датчика, 4,7 кОм подтянуть к питанию

#include "GyverTimer.h"             // подключаем библиотеку таймеров
GTimer periodSms(MS, 15000);        // создаем таймер - интервал опроса наличия СМС в миллисекундах 15000мс=15сек
GTimer periodReset(MS, 3600000);    // создаем таймер - интервал проверки наличия связи 3600000мс=1час
GTimer periodRele(MS, 10000);       // создаем таймер - интервал заднржки работы термореле 10000мс=10сек

#include <SoftwareSerial.h>         // библиотека последовательного порта для связи с SIM800
SoftwareSerial sim800(5, 4);        // Указываем пины подключения SIM800 D5 - RX Arduino (TX SIM800L), D4 - TX Arduino (RX SIM800L)

#include <EEPROM.h>                 // Подключаем библиотеку ЕЕПРОМ

#define INIT_ADDR 1023              // номер резервной ячейки ЕЕПРОМ проверки первого запуска
#define INIT_KEY 55                 // ключ первого запуска. 0-254, на выбор, если что то поменяли и хотим записать в ЕПРОМ, меняем ключ первого запуска

#define Heat 13                     // Пин управления нагрузкой
#define KeyLed 16                   // Пин вкл/выкл подсветки табло
#define KeyTerm 17                  // Пин ручного вкл/откл термореле
#define pinResetArd 15              // Пин Reset Ардуино
#define pinResetSim 6               // Пин Reset Ардуино

int TSens1=0;                       // Переменная для хранения температуры с датчика 1
int TSens2=0;                       // Переменная для хранения температуры с датчика 2
int Temp=18;                        // Переменная для хранения заданной температуры
bool Led=0;                         // состояние подсветки
bool ONOFF=0;                       // состояниt терморегулятора
bool hasmsg=0;                      // Флаг наличия сообщений к удалению
bool flagKeyLed=0;                  // Флаг состояния подсветки табло
bool flagKeyTerm=0;                 // Флаг состояния терморегулятора
bool flagONOFF=0;                   // Флаг кнопки состояния включения терморегулятора
bool flagLed=0;                     // Флаг кнопки подсветки

String phones = "+79022726777, +79226167706, +79043895949";   // Белый список телефонов
String ONOFFS = "OFF";              // Текстовое отображение состояния терморегулятора
String oper_response = "";          // Переменная для хранения оператора GSM модуля
String _response = "";              // Переменная для хранения ответа модуля


/////////////////////
// Стартовые настроки
/////////////////////

void setup() {
  _response.reserve(256);           // Зарезервировали под ответы модема 256 байт, чтобы память не дробилась.
  pinMode (pinResetArd, OUTPUT);    // Настраиваем вывод управления Reset Arduino
  pinMode (pinResetSim, OUTPUT);    // Настраиваем вывод управления Reset SIM800
  pinMode (Heat, OUTPUT);           // Настраиваем вывод управления нагрузкой
  pinMode (KeyLed, INPUT_PULLUP);   // Настраиваем вывод управления подсветкой
  pinMode (KeyTerm, INPUT_PULLUP);  // Настраиваем вывод управления термореле
 
  digitalWrite(pinResetArd, 0);     // Устанавливаем высокий уровень вывода перезагрузки Ардуино
  digitalWrite(pinResetSim, 1);     // Устанавливаем высокий уровень вывода перезагрузки GSM модуля
  digitalWrite (Heat, 0);           // При старте нагрузка выключена
  lcd.init();                       // инициализация табло
  lcd.backlight();                  // включить подсветку
  Serial.begin(115200);               // инициализация связи с ПК
  Serial.println(F("Старт!"));      // Делаем задержку старта программы для регистрации SIM в сети
    Serial.println(F("Load"));
    lcd.home();                     // курсор табло в 0,0
    lcd.print("Load        ");
    lcd.setCursor(0, 1);            // курсор на вторую строку
    for (int i = 0; i < 20; i++) {  // заполнение *
      Serial.print("*");
      delay (1000);
      lcd.write(255); }               
      Serial.println("*");
    lcd.clear();
 
  if (EEPROM.read(INIT_ADDR) != INIT_KEY) {                             // обработка первого запуска. Если ключ в ячейке отличается, то
      EEPROM.write(INIT_ADDR, INIT_KEY);                                // записали ключ первого запуска, чтобы не повторялось
      EEPROM.put(0, Temp);                                              // Записали температуру из настроек в 0 и 1 ячейки Temp int 2 байта
      EEPROM.put(5, ONOFF);                                             // Записали флаг термореле в 5 ячейку1 байт
      Serial.println(F("Запись ЕЕПРОМ"));
  }                                                                     // иначе считали из ЕЕПРОМ
  EEPROM.get(0, Temp);                                                  // прочитали температуру
  EEPROM.get(5, ONOFF);                                                 // прочитали флаг термореле  
  Serial.println(F("Установленная температура: "));
  Serial.println(Temp);
  Serial.println(F("Состояние термореле: "));
  Serial.println(ONOFF);

  
  sim800.begin(9600);                                                   // инициализация связи Arduino и SIM800
  Serial.println(F("\nПроверяем связь с модулем GSM"));                 // Проверка связи с модулем
  _response = sendATCommand("AT", true);                                // Запрашиваем связь с модулем
  _response.trim();                                                     // Убираем из ответа пробелы
  _response.replace("\r\n", "");
  Serial.println(_response);                                        // убираем переносы строк
  lcd.home();                                                           // курсор табло в 0,0
  if (_response.endsWith("OK")) {                                       // Если ответ заканчивается на "ОК"
    Serial.println(F("Модуль GSM на связи"));
    lcd.print("Modul GSM online        ");}                                 
  else {
    Serial.println(F("Модуль GSM не отвечает"));
    lcd.print("Modul GSM offline   ");}
  _response=sendATCommand("AT+CSQ", true);                              // Проверка уровня сигнала GSM. Если 5 и меньше уровень слабый
  _response.trim();                                                     // Убираем из ответа пробелы
  _response.replace("\r\n", "");                                        // убираем переносы строк
  _response=_response.substring(6);                                     // Готовим ответ к выводу, убираем первые 6 символов
  _response.replace("OK", "");                                          // убираем из ответа ОК  
  if (_response.toInt() < 5) {
    Serial.print(F("\nСлабый уровень сигнала: "));                      // Выводим уровень сигнала
    Serial.println(_response.toInt());}  
  else {Serial.print(F("\nХороший уровень сигнала: "));
    Serial.println(_response.toInt());} 
  delay(1000);
  lcd.clear();
  _response=sendATCommand("AT+COPS?", true);                            // Запрашиваем к какому олператору подключена СИМ
  _response.trim();                                                     // Убираем из ответа пробелы
  _response.replace("\r\n", "");                                        // убираем переносы строк
  _response=_response.substring(11);                                    // Готовим ответ к выводу, убираем первые 11 символов
  _response.replace("OK", "");                                          // убираем из ответа ОК  
  
  if (_response.length() < 5) {                                         // выводим название или отсутствие соединения
    lcd.print("GSM error        ");
    Serial.println(F("\nСИМ карта не зарегстрировалась в сети"));}
  else{
    Serial.print(F("\nУстановлена связь с оператором: "));
    Serial.println(_response);
    oper_response=_response;                                            // Сохраняем имя оператора СС для дальнейшего контроля связи
    lcd.print("GSM OP: " + _response);
  }
  
  
  sendATCommand("AT+CMGDA=\"DEL ALL\"", true);                          // При включении удаляем все SMS из SIM800, чтобы не забивать память
  sendATCommand("AT+CMGF=1;&W", true);                                  // Включаем текстовый режима SMS (Text mode)
  periodSms.start();                                                    // перезапускаем таймер проверки СМС
  periodReset.start();                                                  // перезапускаем таймер проверки связи c модулем GSM
  delay(500);
  lcd.clear();

}


////////////////
//Цикл программы
////////////////

void loop() {             
  sensor1.requestTemp();                                                // запросить температуру 1 датчика
  sensor2.requestTemp();                                                // запросить температуру 2 датчика
  

  if (periodSms.isReady()) {                                            // По сработке таймера проверяем наличие новых СМС
    do {
    Serial.print(millis());
    Serial.println(F(" Проверяем СМС"));
    _response = sendATCommand("AT+CMGL=\"REC UNREAD\",1", true);        // Отправляем запрос чтения непрочитанных сообщений
    if (_response.indexOf("+CMGL: ") > -1) {                            // Если есть хоть одно, получаем его индекс
        Serial.println(F("Есть соощение"));
        int msgIndex = _response.substring(_response.indexOf("+CMGL: ") + 7, _response.indexOf("\"REC UNREAD\"", _response.indexOf("+CMGL: ")) - 1).toInt();
        Serial.println(msgIndex);
        char i = 0;                                                     // Объявляем счетчик попыток чтения СМС
        do {
          i++;                                                          // Увеличиваем счетчик
          _response = sendATCommand("AT+CMGR=" + (String)msgIndex + ",1", true);  // Пробуем получить текст SMS по индексу
          _response.trim();
          Serial.println(_response);                                             // Убираем пробелы в начале/конце
          if (_response.endsWith("OK")) {                               // Если ответ заканчивается на ОК:
            if (!hasmsg) hasmsg = true;                                 // Ставим флаг наличия сообщений для удаления
            sendATCommand("AT+CMGR=" + (String)msgIndex, true);         // Делаем сообщение прочитанным
            sendATCommand("\n", true);                                  // Перестраховка - вывод новой строки
            parseSMS(_response);                                        // Отправляем текст сообщения на обработку
            break;                                                      // Выход из do{}
          }
          else {                                                        // Если сообщение не заканчивается на OK
            Serial.println(F("Error answer"));                          // Какая-то ошибка
            sendATCommand("\n", true);                                  // Отправляем новую строку и повторяем попытку
            if (i=4){
              sendATCommand("AT+CMGD=" + (String)msgIndex + ",0", true);  //Если не смогли прочитать 5 раз - удаляем сообщение
            }
          }
        } while (i < 5);
        break;
      }
      else {                                                            // иначе
        if (hasmsg) {                                                   // если флаг наличия сообщений 1, то
          sendATCommand("AT+CMGDA=\"DEL READ\"", true);                 // удаляем все прочитанные сообщения
          hasmsg = false;                                               // меняем флаг наличия сообщений на 0
        }
        break;                                                          //выход из условия
      }
    } while (1);
  }

  if (periodReset.isReady()) {                                          // По сработке таймера проверяем связь с модулем, оператором.
    Serial.println(F("Проверяем связь"));                               // Проверка связи с оператором
    _response=sendATCommand("AT+COPS?", true);                          // Запрашиваем к какому олператору подключена СИМ
    _response.trim();                                                   // Убираем из ответа пробелы
    _response.replace("\r\n", "");                                      // убираем переносы строк
    _response=_response.substring(11);                                  // Готовим ответ к выводу, убираем первые 11 символов
    _response.replace("OK", "");                                        // убираем из ответа ОК  
    if (_response != oper_response) {                                   // Если связи нет, то
      Serial.println(F("SIM карта не в сети"));
      Serial.println(F("Перезагрузка модуля GSM"));
      digitalWrite(pinResetSim, 0);                                     // перезагружамем модуль SIM800
      delay(3000);                                                      // Ждем для перезагрузки модуля и регистрации в сети
      digitalWrite(pinResetSim, 1);                                     
      delay(25000);
      sendATCommand("AT", true);
      }    
      else{                                                             // иначе проверка закончена
      Serial.print(F("Установлена связь с оператором: "));
      Serial.println(_response);}
    _response = sendATCommand("AT", true);                              // Проверяем связь с модулем, запрашиваем связь с модулем
    _response.trim();                                                   // Убираем из ответа пробелы
    _response.replace("\r\n", "");                                      // убираем переносы строк
    if (_response.endsWith("OK")) {                                     // Если ответ заканчивается на "ОК"
      Serial.println(F("Модуль на связи"));}                            // модуль на связи, переходим к проверке связи с оператором     
      else {Serial.println(F("Проблема инициализации AT команд"));      // иначе перезагружаем Ардуино
      Serial.println(F("Перезагрузка процессора")); 
      delay(1000);
      digitalWrite(pinResetArd, 1);
      }
  }

  if (sim800.available())   {                                           // Если модем, что-то отправил...
    Serial.println(F("Данные от GSM модуля:"));
    _response = waitResponse();                                         // Получаем ответ от модема для анализа
    _response.trim();                                                   // Убираем лишние пробелы в начале и конце
    Serial.println(_response);                                          // Выводим в монитор порта
    if (_response.indexOf("+CMTI:")>-1) {                               // Пришло сообщение об отправке SMS, теперь нет необходимости обрабатываеть SMS здесь, достаточно просто
      periodSms.start();                                                // сбросить счетчик автопроверки и в следующем цикле все будет обработано
    }
  }
 
  if (sensor1.readTemp()) TSens1 = sensor1.getTemp();                   // Если сенсор готов выдать температуру, записываем температуру 1 датчика в переменную
  if (sensor2.readTemp()) TSens2 = sensor2.getTemp();                   // записываем температуру 2 датчика в переменную
  
  if (ONOFF == 1) {                                                     // Переводим состояние термореле в текстовый вид для вывода на индикатор
    ONOFFS = "ON ";
    }  
    else {
      ONOFFS = "OFF";
      }

  if (Led == 1){                                                        // Включение подсветки по флагу
    lcd.backlight();                                                    // включить подсветку
    }
    else {
      lcd.noBacklight();                                                // вsключить подсветку
      }
  
  if (periodRele.isReady()) {                                           // С периодичностью проверяем термореле, чтобы переключения были не слишком частые
    if (TSens2 <= Temp && ONOFF == 1) {                                 // Термореле
      digitalWrite (Heat, 1);                                           // если температура ниже заданной и термореле включено, то нагрузка включена, на индикаторе ON
      lcd.setCursor(15, 1);                                             // курсор на вторую строку 15 поле
      lcd.print("*");                                                   // выдаем на индикатор состояние обогрева
      }
      else { 
        digitalWrite (Heat, 0);                                         // иначе нагрузку выключаем и выводим OFF
        lcd.setCursor(15, 1);                                           // курсор на вторую строку 15 поле
        lcd.print(" ");                                                 // выдаем на индикатор состояние обогрева
      }
  }
  
  //Выводим данные на индикатор
  lcd.home();                       // курсор табло в 0,0
  lcd.print("OUT ");                // данные 1 датчика
  lcd.print(TSens1);                // выводим температуру
  lcd.write(223);                   // символ градуса
  lcd.print("C  ");                 // С и пара пробелов для очистки
  lcd.setCursor(0, 1);              // курсор на вторую строку
  lcd.print("IN  ");                // данные 2 датчика
  lcd.print(TSens2);                // температура 2 датчика
  lcd.write(223);                   // символ градуса
  lcd.print("C  ");                 // С
  lcd.setCursor(11, 0);             // курсор на первую строку 11 поле
  lcd.print(Temp);                  // выдаем на индикатор заданную температуру
  lcd.write(223);                   // символ градуса
  lcd.print("C  ");                 // С
  lcd.setCursor(11, 1);             // курсор на вторую строку 11 поле
  lcd.print(ONOFFS);                // выдаем на индикатор состояние обогрева


//обработка нажатия кнопок
  flagLed = !digitalRead(KeyLed);                             // обработка кнопки вкл/выкл подсветки табло
  if (flagLed && !flagKeyLed) {                               // обработчик нажатия
    flagKeyLed = true;
    Serial.println(F("Нажата кнопка подсветки"));
    Led = !Led;
  }
  if (!flagLed && flagKeyLed) {                               // обработчик отпускания
    flagKeyLed = false;  
  }
  
  flagONOFF = !digitalRead(KeyTerm);                          // обработка вкл/выкл термореле по кнопке читаем во флаг состояние вывода к которому подключена кнопка, как только нажали во флаг запишется 1
  if (flagONOFF && !flagKeyTerm) {                            //
    flagKeyTerm = true;
    Serial.println(F("Нажата кнопка термореле"));
    ONOFF = !ONOFF;
    EEPROM.put(5, ONOFF);                                     // Записали флаг термореле в ЕЕПРОМ
    periodRele.reset();                                       // Сбрасываем таймер задержки реле
  }
  if (!flagONOFF && flagKeyTerm) {                            // обработчик отпускания
    flagKeyTerm = false;  
  }


  
  if (Serial.available())  {                                  // Ожидаем команды по Serial...
    sim800.write(Serial.read());                              // ...и отправляем полученную команду модему
  };
}

/////////////////
//Функции
/////////////////

String sendATCommand(String cmd, bool waiting) {              // Функция отправки АТ команд в SIM800
  String _resp = "";                                          // Переменная для хранения результата
  sim800.println(cmd);                                        // Отправляем команду модулю
  if (waiting) {                                              // Если необходимо дождаться ответа...
    _resp = waitResponse();                                   // ... ждем, когда будет передан ответ
    if (_resp.startsWith(cmd)) {                              // Убираем из ответа дублирующуюся команду Если ответ начинается с команды то
      _resp.remove(0, _resp.indexOf("\r", cmd.length()) + 2); // удаляем с начала строки до переноса \r
    }
  //Serial.println(_resp);                                    // Дублируем ответ в монитор порта
  }
  return _resp;                                               // Возвращаем результат. Пусто, если проблема
}


String waitResponse() {                                       // Функция ожидания ответа и возврата полученного результата
  String _resp = "";                                          // Очищаем переменная для хранения результата
  long _timeout = millis() + 10000;                           // Переменная пириода ожидания ответа (10 секунд)
  while (!sim800.available() && millis() < _timeout){ };      // Ждем ответа 10 секунд, если пришел ответ или наступил таймаут, то... / Пока в буфере пусто и время не вышло ждем
  if (sim800.available()) {                                   // Если есть, что считывать...
    _resp = sim800.readString();                              // ... считываем и запоминаем
  }
  else {                                                      // Если пришел таймаут, то...
    Serial.println(F("Time out."));                           // ... оповещаем об этом и...
  }
  return _resp;                                               // ... возвращаем результат. Пусто, если проблема
}


void parseSMS(String msg) {                                   // Парсим SMS
  String msgheader  = "";
  String msgbody    = "";
  String msgphone   = "";
  msg = msg.substring(msg.indexOf("+CMGR: "));                // отбрасываем от сообщения (+CMGR: )
  msgheader = msg.substring(0, msg.indexOf("\r"));            // Выдергиваем заголовок
  msgbody = msg.substring(msgheader.length() + 2);            // Отбрасываем заголовок
  msgbody = msgbody.substring(0, msgbody.lastIndexOf("OK"));  // Выдергиваем текст SMS
  msgbody.trim();                                             // убираем пробелы
  int firstIndex = msgheader.indexOf("\",\"") + 3;
  int secondIndex = msgheader.indexOf("\",\"", firstIndex);
  msgphone = msgheader.substring(firstIndex, secondIndex);    // выдергиваем номер телефона из заголовка

  Serial.println("Phone: " + msgphone);                       // Выводим номер телефона
  Serial.println("Message: " + msgbody);                      // Выводим текст SMS

  if (msgphone.length() > 6 && phones.indexOf(msgphone) > -1){ // Если длина номера больше 6 и телефон в белом списке, то...
    comand(msgbody, msgphone);                                 // ...выполняем подпрограмму 
  }
  else {
    Serial.println(F("Unknown phonenumber"));
    }
}


void comand (String result, String phone) {                   // Функция обработки полученных из СМС команд и отправки ответных СМС
  bool correct = false;                                       // Для оптимизации кода, переменная корректности команды
  sim800.println("AT+CMGS=\"" + phone + "\"");
  delay(300);
  if (result.length() == 6) {
    if (result == "Termon") {
      ONOFF = 1;
      ONOFFS = "ON ";
      EEPROM.put(5, ONOFF);                                   // Записали флаг термореле в ЕЕПРОМ
      sim800.println("Termorele ON");
      delay(300);
      sendSMS();
      correct = true;                                         // Флаг корректности команды
    }
    if (result == "Termof") {
      ONOFF = 0;
      ONOFFS = "OFF";
      EEPROM.put(5, ONOFF);                                   // Записали флаг термореле в ЕЕПРОМ
      sim800.println("Termorele OFF");
      delay(300);
      sendSMS();
      correct = true;                                         // Флаг корректности команды
    }
    if (result == "Status") {
      sim800.println("Termorele: " + (String)ONOFFS);
      delay(300);
      sendSMS();
      correct = true;                                         // Флаг корректности команды
    }
    if (result.startsWith("Temp")) {
      Temp = ((String)result.substring(4)).toInt();
      Temp = constrain(Temp, 0, 30);                          // Ограничиваем диапазон температуры между 0 и 30
      EEPROM.put(0, Temp);                                    // Записали новую температуру в ЕЕПРОМ
      sim800.println("Temperature setting: " + (String)Temp);
      delay(300);
      sim800.println("Termorele: " + (String)ONOFFS);
      delay(300);
      sendSMS();
      correct = true;                                         // Флаг корректности команды
    }
  }
  if (!correct) {
    sim800.println("Incorrect command: " + result);
    delay(300);
    sim800.println("Comands true:");
    delay(300);
    sim800.println("TempXX,");
    delay(300);
    sim800.println("Termon,");
    delay(300);
    sim800.println("Termof,");
    delay(300);
    sim800.print("Status.");
    delay(300);
    sim800.print((char)26);
    delay(300);
    Serial.println(F("SMS send complete"));
  }
}

void sendSMS () {                                             // Отправка показаний
    sim800.println("Temp OUT: " + (String)TSens1);
    delay(300);
    sim800.println("Temp IN: " + (String)TSens2);
    delay(300);
    sim800.println("Temperature Set: " + (String)Temp);
    delay(300);
    sim800.print((char)26);
    delay(300);
    Serial.println(F("SMS send complete"));
}