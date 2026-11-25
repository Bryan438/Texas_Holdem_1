#ifndef _CONNECTION_LIMITS_H_
#define _CONNECTION_LIMITS_H_

#include <map>
#include <mutex>
#include <string>

// How many connections the server holds open, in all and per address, so
// that one machine on the internet cannot open thousands of them (every
// connection has its own thread).
//
// A connection is counted when it is accepted, under its address. Behind a
// reverse proxy (Caddy) every browser arrives from 127.0.0.1: those are
// counted with no address at first, and once the request says who the
// browser really is (X-Forwarded-For), `rekey` moves it under that address.
class connection_limits{
  public:
    int max_total = 200;
    int max_per_address = 20;

    // Count a new connection. False: over a limit, turn it away.
    bool admit(int id, const std::string& address){
      std::lock_guard<std::mutex> guard(lock);
      if(total >= max_total || over(address)){
        return false;
      }
      total++;
      if(!address.empty()) per_address[address]++;
      counted[id] = address;
      return true;
    }

    // Count a connection admitted with no address under its real one.
    bool rekey(int id, const std::string& address){
      std::lock_guard<std::mutex> guard(lock);
      auto it = counted.find(id);
      if(it == counted.end() || address.empty() || it->second == address){
        return true;
      }
      if(over(address)){
        return false;
      }
      forget(it->second);
      per_address[address]++;
      it->second = address;
      return true;
    }

    // The connection closed (or was never counted: then nothing happens).
    void release(int id){
      std::lock_guard<std::mutex> guard(lock);
      auto it = counted.find(id);
      if(it == counted.end()) return;
      forget(it->second);
      counted.erase(it);
      total--;
    }

    int open() const{
      std::lock_guard<std::mutex> guard(lock);
      return total;
    }

  private:
    mutable std::mutex lock;
    int total = 0;
    std::map<std::string, int> per_address;
    std::map<int, std::string> counted;   // connection -> address it is counted under

    bool over(const std::string& address) const{
      if(address.empty()) return false;
      auto it = per_address.find(address);
      return it != per_address.end() && it->second >= max_per_address;
    }
    void forget(const std::string& address){
      if(address.empty()) return;
      if(--per_address[address] <= 0) per_address.erase(address);
    }
};

// The browser's address from an X-Forwarded-For header set by our own proxy:
// the last entry (the one the proxy added; earlier ones came from the client
// and can be anything). "" when there is none.
inline std::string forwarded_client(const std::string& header){
  std::string last = header.substr(header.find_last_of(',') == std::string::npos ? 0 : header.find_last_of(',') + 1);
  size_t start = last.find_first_not_of(' ');
  if(start == std::string::npos) return "";
  size_t end = last.find_last_not_of(' ');
  return last.substr(start, end - start + 1);
}

#endif
