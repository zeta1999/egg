#include "egg0/egg0.h"
#include "egg0/lexer.h"
#include "egg0/program.h"

int main(int argc, char *argv[])
{
  try {
    std::cout << "This is egg" << std::endl;
    size_t argn = argc;
    std::vector<std::string> args(argn);
    for (auto i = 0; i < argn; ++i) {
      args[i] = argv[i];
    }
    egg0::Program::main(args);
    return 0;
  }
  catch (const std::runtime_error& exception) {
    std::cerr << "Exception caught: " << exception.what() << std::endl;
    return 1;
  }
}
