#include <cstdio>
#include "test_framework.h"
#include "transport.h"

int main(){
  setvbuf(stdout, NULL, _IONBF, 0);
  // Silence the hex dumps of every packet.
  transport::get_instance()->set_verbose(false);

  int failed_tests = 0;
  for(const test_case& t : test_registry()){
    int before = test_failures();
    t.fn();
    bool ok = test_failures() == before;
    if(!ok){
      failed_tests++;
    }
    printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
  }
  printf("\n%zu tests, %d failed\n", test_registry().size(), failed_tests);
  return failed_tests == 0 ? 0 : 1;
}
