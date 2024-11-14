#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>

// Макрос для определения максимального размера буфера для передачи данных
#define BUF_SIZE 1024
// Макрос для определения максимального количества попыток передачи файла
#define MAX_ATTEMPTS 5

// Структура для хранения данных о файле
struct FileData {
    char filename[256];  // Имя файла
    int filesize;         // Размер файла
};

// Структура для сообщения об успехе/неудаче передачи файла
struct SuccessMessage {
    bool success; // Флаг успешной передачи файла
};

// Функция для отправки файла с проверкой целостности
// Принимает сокет, имя файла и адрес сервера.
// Возвращает true, если файл успешно отправлен, иначе false.
bool sendFile(int sockfd, const char* filename, struct sockaddr_in serverAddr) {
    // Открываем файл для чтения в бинарном режиме
    std::ifstream file(filename, std::ios::binary);
    // Проверяем, удалось ли открыть файл
    if (!file.is_open()) {
        std::cerr << "Ошибка открытия файла: " << filename << std::endl;
        return false;
    }

    // Получаем размер файла
    file.seekg(0, std::ios::end);
    int filesize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Создаем структуру с данными о файле
    FileData fileData;
    strcpy(fileData.filename, filename); // Копируем имя файла в структуру
    fileData.filesize = filesize;      // Записываем размер файла в структуру

    // Отправляем информацию о файле на сервер
    if (sendto(sockfd, &fileData, sizeof(fileData), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Ошибка отправки информации о файле" << std::endl;
        return false;
    }

    // Отправляем содержимое файла на сервер
    char buffer[BUF_SIZE]; // Буфер для хранения данных файла
    int bytesSent = 0;  // Счетчик отправленных байт
    while (bytesSent < filesize) {
        // Читаем данные из файла в буфер
        file.read(buffer, BUF_SIZE);
        int bytesRead = file.gcount(); // Получаем количество прочитанных байт
        // Если прочитанные байты равны 0, значит, файл закончился
        if (bytesRead == 0) {
            break;
        }
        // Отправляем данные из буфера на сервер
        if (sendto(sockfd, buffer, bytesRead, 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "Ошибка отправки данных файла" << std::endl;
            return false;
        }
        // Обновляем счетчик отправленных байт
        bytesSent += bytesRead;
    }

    // Проверяем, были ли отправлены все данные файла
    if (bytesSent != filesize) {
        std::cerr << "Ошибка передачи файла: отправлено не все данные" << std::endl;
        return false;
    }

    return true; // Файл успешно отправлен
}

// Главная функция программы
int main(int argc, char *argv[]) {
    // Проверяем, передан ли порт в качестве аргумента командной строки
    if (argc != 2) {
        std::cerr << "Неверный формат запуска: ./client <port>" << std::endl;
        return 1;
    }

    // Получаем порт из аргумента командной строки
    int port = atoi(argv[1]);

    // Создаем UDP-сокет
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    // Проверяем, удалось ли создать сокет
    if (sockfd < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    // Заполняем структуру с адресом сервера
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr)); // Очищаем структуру
    serverAddr.sin_family = AF_INET;          // Устанавливаем семейство адресов
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Устанавливаем адрес сервера
    serverAddr.sin_port = htons(port);           // Устанавливаем порт сервера

    // Открываем директорию с файлами JPEG
    DIR *dir = opendir("../jpeg");
    // Проверяем, удалось ли открыть директорию
    if (dir == NULL) {
        std::cerr << "Ошибка открытия директории" << std::endl;
        return 1;
    }

    struct dirent *ent; // Структура для хранения информации о файле в директории
    int attemptCount = 0; // Счетчик попыток передачи файла

    // Цикл по всем файлам в директории
    while ((ent = readdir(dir)) != NULL) {
        // Пропускаем файлы "." и ".."
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        // Проверяем, является ли запись файлом
        struct stat sb;
        std::string filepath = "../jpeg/" + std::string(ent->d_name);
        if (stat(filepath.c_str(), &sb) == -1) {
            std::cerr << "Ошибка получения информации о файле: " << filepath << std::endl;
            continue;
        }
        if (S_ISREG(sb.st_mode)) {
            std::cout << "Отправка файла: " << filepath << std::endl;
            attemptCount = 0; // Сбрасываем счетчик попыток
            bool success = false; // Флаг успешной передачи файла

            // Попытки передачи файла
            do {
                attemptCount++; // Увеличиваем счетчик попыток
                // Если число попыток превысило максимальное значение, прекращаем попытки
                if (attemptCount > MAX_ATTEMPTS) {
                    std::cerr << "Превышено максимальное количество попыток передачи файла: " << filepath << std::endl;
                    break;
                }
                // Отправляем файл на сервер
                if (!sendFile(sockfd, filepath.c_str(), serverAddr)) {
                    std::cerr << "Ошибка передачи файла: " << filepath << std::endl;
                    continue;
                }
                
                // Получение сообщения об успехе от сервера
                SuccessMessage successMsg;
                socklen_t addrLen = sizeof(serverAddr);
                if (recvfrom(sockfd, &successMsg, sizeof(successMsg), 0, (struct sockaddr*)&serverAddr, &addrLen) < 0) {
                    std::cerr << "Ошибка получения сообщения об успехе" << std::endl;
                    return 1;
                }

                success = successMsg.success;
            } while (!success); // Повторяем цикл, пока файл не будет отправлен успешно

            if (success) {
                std::cout << "Файл " << filepath << " успешно отправлен" << std::endl;
            }
        }
    }

    // Закрываем директорию
    closedir(dir);
    // Закрываем сокет
    close(sockfd);
    return 0;
}
