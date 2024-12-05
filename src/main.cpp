#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <vector>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>

using boost::asio::ip::tcp;

#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32]; // supondo um ID de sensor de até 32 caracteres
    std::time_t timestamp; // timestamp UNIX
    double value; // valor da leitura
};
#pragma pack(pop)

std::vector<std::string> split_message(const std::string& message, char delimiter){
  std::vector<std::string> tokens;
  std::istringstream stream(message);
  std::string token;
  while (std::getline(stream, token, delimiter)){
    tokens.push_back(token);
  }
  return tokens;
}

std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

class LogManager {
  public:
    void process_log(std::vector<std::string>& tokens){
      LogRecord logrecord_;
      std::strncpy(logrecord_.sensor_id,tokens[1].c_str(), sizeof(logrecord_.sensor_id) - 1);
      logrecord_.sensor_id[sizeof(logrecord_.sensor_id) - 1] = '\0';
      logrecord_.timestamp = string_to_time_t(tokens[2]);
      logrecord_.value = std::stod(tokens[3]);

      std::lock_guard<std::mutex> lock(mutex_); // Protege o mapa
      // Verifica se o sensor já está mapeado
        if (file_map_.find(logrecord_.sensor_id) == file_map_.end()) {
            // Se não estiver, cria um novo fluxo de arquivo
            std::string filename = tokens[1] + ".dat";
            std::fstream file(filename, std::fstream::out | std::fstream::in | std::fstream::binary | std::fstream::app);

            if (!file.is_open()) {
                // Se o arquivo não existir, cria e reabre
                file.open(filename, std::fstream::out | std::fstream::binary);
                file.close();
                file.open(filename, std::fstream::out | std::fstream::in | std::fstream::binary | std::fstream::app);
            }

            // Adiciona o fluxo ao mapa
            file_map_[logrecord_.sensor_id] = std::move(file);
            
        }

      // Escreve o registro no arquivo
      std::fstream& file = file_map_[logrecord_.sensor_id];
      file.write((char*)&logrecord_, sizeof(LogRecord));
      file.flush(); // Garante que os dados sejam salvos imediatamente
      std::cout << "Log registrado para o sensor: " << logrecord_.sensor_id << std::endl;
  
    }


    std::string process_get(std::vector<std::string>& tokens){
      int n_reg = std::stoi(tokens[2]);
      std::lock_guard<std::mutex> lock(mutex_); // Protege o mapa
      if (file_map_.find(tokens[1]) == file_map_.end()){
        return "ERROR|INVALID_SENSOR_ID\r\n";
      } 

      std::fstream& file = file_map_[tokens[1]];

      if (file.is_open()){

        // Imprime a posição atual do apontador do arquivo (representa o tamanho do arquivo)
		    int file_size = file.tellg();

        // Recupera o número de registros presentes no arquivo
		    int n = file_size/sizeof(LogRecord);
		    std::cout << "Num records: " << n << " (file size: " << file_size << " bytes)" << std::endl;

        //Retorna somente o número de registros presentes no arquivo caso n_reg seja maior que o n 
        if(n_reg > n){
          n_reg = n;
        }
        
        LogRecord logrecord;
        std::string send;
        std::string str_n = std::to_string(n);
        send = str_n + ";";
        for(int i = n_reg; i > 0; i-- ){

          file.seekg((-i)*sizeof(LogRecord), std::ios_base::end);
          file.read((char*)&logrecord, sizeof(LogRecord));

          // Converte os valores do registro para string
          std::string str_timestamp = time_t_to_string(logrecord.timestamp);
          std::string str_value = std::to_string(logrecord.value);
          
          // Formata a string no formato solicitado
          send = send + "|" + str_timestamp + "|" + str_value;
        }

        return send + "\r\n";

      }       
      else{
        std::cout << "Error opening file!" << std::endl;
        return "ERROR_OPEN_FILE";
      }     
    }

  private:
    std::unordered_map<std::string, std::fstream> file_map_; // Mapeia sensor_id para o fluxo de arquivo
    std::mutex mutex_; // Protege o mapa contra acesso simultâneo
  
};

class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket, LogManager& logger)
    : socket_(std::move(socket)), logger_(logger)
  {
  }

  void start()
  {
    read_message();
  }

private:
  void read_message()
  {
    auto self(shared_from_this());
    boost::asio::async_read_until(socket_, buffer_, "\r\n",
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            std::istream is(&buffer_);
            std::string message(std::istreambuf_iterator<char>(is), {});
            std::cout << "Received: " << message << std::endl;
            // Processamento da mensagem divide em tokens
            std::vector<std::string> tokens = split_message(message, '|');
            // Processa LOG or GET
            if(tokens[0] == "LOG"){
              logger_.process_log(tokens);
              write_message(message);
            }
            else{
              write_message(logger_.process_get(tokens));
            }
            
          }
        });
  }

  void write_message(const std::string& message)
  {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(message),
        [this, self, message](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            read_message();
          }
        });
  }

  tcp::socket socket_;
  boost::asio::streambuf buffer_;
  LogManager& logger_; 
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), logger_()
  {
    accept();
  }

private:
  void accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<session>(std::move(socket),logger_)->start();
          }

          accept();
        });
  }

  tcp::acceptor acceptor_;
  LogManager logger_;
};

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: chat_server <port>\n";
    return 1;
  }

  boost::asio::io_context io_context;

  server s(io_context, std::atoi(argv[1]));

  io_context.run();

  return 0;
}
