#include "yolk/platform.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include <unordered_map>

#include "ovum/ovum.h"
#include "ovum/node.h"
#include "ovum/module.h"
#include "ovum/program.h"
#include "ovum/dictionary.h"
#include "ovum/function.h"

#include "yolk/macros.h"
#include "yolk/exceptions.h"
#include "yolk/files.h"
#include "yolk/streams.h"
#include "yolk/strings.h"

#include "yolk/functions.h"
