//Copyright 2022 wm8
#include "Server.h"
bool Server::isRunning;
json* Server::data;

void Server::NotFound(struct evhttp_request *request,
                      [[maybe_unused]] void *params) {
  evhttp_send_error(request,
                    HTTP_NOTFOUND, "Not Found");
}
//Метод отправки в качестве ответа json
void SendJSON(struct evhttp_request *req, json& response)
{
  //Выделяем память
  struct evbuffer *buffer;
  buffer = evbuffer_new();
  //Записываем в буффер json
  evbuffer_add_printf(buffer, response.dump().c_str(), 8);
  //Настройка параметров запроса
  //Такие параметры должны быть при ответе json'ом
  evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                    "text/html");
  //В ответе так же должен быть прописан код ответа (по типу 404, 500 и т.д)
  //В нашем случае код ответа - ok (или 200)
  evhttp_send_reply(req, HTTP_OK, "OK", buffer);
  //Очищаем динамически выделенную память
  evbuffer_free(buffer);
}

void Server::Suggest(struct evhttp_request *req,[[maybe_unused]] void *params) {
  json response;
  if (data == nullptr) {
    response["error"] = "data is not initialized!";
    SendJSON(req, response);
    return;
  }
  if (evhttp_request_get_command(req) != EVHTTP_REQ_POST) {
    response["error"] = "request method must be POST";
    SendJSON(req, response);
    return;
  }
  // Читаем тело запроса как буффер
  auto *postBuf = evhttp_request_get_input_buffer(req);
  try
  {
    json postJSON;
    {
      //Получаем длину буфера
      size_t len = evbuffer_get_length(postBuf);

      char *str;
      str = static_cast<char *>(malloc(len + 1));
      evbuffer_copyout(postBuf, str, len);
      str[len] = '\0';
      postJSON = json::parse(str);
    }
    //Если в json нет ключа input
    if (!postJSON.contains("input")) {
      //Возращаем ошибку
      response["error"] = "wrong json";
      SendJSON(req, response);
      return;
    }
    //Достаем слово из ключа input
    auto word = postJSON["input"].get<std::string>();
    //Создаем json ответ
    std::string res;
    //Создаем массив в ключ suggestions
    response["suggestions"] = json::array();
    //Создаем массив из пары данных (число, строка)
    //Т.е в 1 элементе массива одновременно хранятся
    //Число и строка, где число - cost, строка - name
    std::vector<std::pair<int, std::string>> words;
    //Проходим по всем элементам массива
    for (auto &elm : *data)
      //Если id элемента явлется искомым словом
      if (elm["id"].get<std::string>() == word)
        //Записываем в массив пар
        words.emplace_back(elm["cost"].get<int>(),
                           elm["name"].get<std::string>());
    //Используем кастомный фильтр для сортировки данных
    struct
    {
      bool operator()(std::pair<int, std::string> a,
                      std::pair<int, std::string> b) const {
        return a.first < b.first;
      }
    } customLess;
    //Сортируем данные
    std::sort(words.begin(), words.end(), customLess);
    int i = 0;
    //Для каждого элемента массива пар
    for (auto &_p : words) {
      //Создаем свой json объект
      json elm;
      //Записываем название
      elm["text"] = _p.second;
      //Позицию
      elm["position"] = i;
      //Добавляем json объект в json массив
      response["suggestions"].push_back(elm);
      i++;
    }

  } catch (json::parse_error &er) {
    response["error"] = er.what();
  }
  //Отправляем ответом json
  SendJSON(req, response);
}

Server::Server(const char *address, const int port)
{
  isRunning = true;
  struct event_base *ebase;
  struct evhttp *server;
  ebase = event_base_new ();
  server = evhttp_new (ebase);
  //Вот эта штучка настраивает ответ при переходе по ссылке /v1/api/suggest
  //В случае перехода по нужному пути вызывается метод Suggest
  evhttp_set_cb (server, "/v1/api/suggest", Suggest, 0);
  //Если пользователь перешел по какому то другому пути
  //Вызывается метод NotFound
  evhttp_set_gencb (server, NotFound, 0);
  //Если не удалось запустить сервер
  if (evhttp_bind_socket (server, address, port) != 0)
    std::cout << "Failed to init http server." << std::endl;

  //Очистка памяти
  event_base_dispatch(ebase);
  evhttp_free (server);
  event_base_free (ebase);
}
