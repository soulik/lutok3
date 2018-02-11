#include "lutok3"

#include <iostream>

namespace lutok3 {
    State::State()
    {
        state = luaL_newstate();
    }

    State::State(lua_State * luaState)
        : state(luaState)
    {
        stateOwner = false;
    }

    State::~State()
    {
        if (stateOwner)
        {
            lua_close(state);
        }
    }

    void State::openLibs()
    {
        luaL_openlibs(state);
    }

    int State::error()
    {
        return lua_error(state);
    }

    int State::error(const char * fmt, ...)
    {
        return luaL_error(state, fmt);
    }

    int State::gc(GCType type, int data)
    {
        return lua_gc(state, static_cast<int>(type), data);
    }

    void State::loadString(const std::string & code)
    {
        int rc = luaL_loadstring(state, code.c_str());
        switch (rc)
        {
        case LUA_ERRSYNTAX:
            throw std::runtime_error("Syntax error");
            break;
        case LUA_ERRMEM:
            throw std::runtime_error("Memory allocation error");
            break;
        case LUA_ERRGCMM:
            throw std::runtime_error("Error while running __gc metamethod");
            break;
        case LUA_OK:
        default:
            break;
        }
    }

    void State::loadFile(const fs::path & filePath, const std::string & mode)
    {
        const char * modePtr = nullptr;
        if (!mode.empty())
        {
            modePtr = mode.c_str();
        }

        int rc = luaL_loadfilex(state, filePath.string().c_str(), modePtr);
        switch (rc)
        {
        case LUA_ERRSYNTAX:
            throw std::runtime_error("Syntax error");
            break;
        case LUA_ERRMEM:
            throw std::runtime_error("Memory allocation error");
            break;
        case LUA_ERRGCMM:
            throw std::runtime_error("Error while running __gc metamethod");
            break;
        case LUA_ERRFILE:
            throw std::runtime_error("Error loading file");
            break;
        case LUA_OK:
        default:
            break;
        }
    }

    void State::doString(const std::string & code)
    {
        loadString(code);
        pcall();
    }

    void State::doFile(const fs::path & filePath, const std::string & mode)
    {
        loadFile(filePath, mode);
        pcall();
    }

    void State::newTable()
    {
        lua_newtable(state);
    }

    void State::newTable(int nArrayElements, int nOtherElements)
    {
        lua_createtable(state, nArrayElements, nOtherElements);
    }

    void State::call(int nArgs, int nResults)
    {
        lua_call(state, nArgs, nResults);
    }

    void State::pcall(int nArgs, int nResults, Index handlerIndex)
    {
        int rc = lua_pcall(state, nArgs, nResults, handlerIndex);
        switch (rc)
        {
        case LUA_ERRRUN:
            throw std::runtime_error("Runtime error:\n" + static_cast<std::string>(get()));
            break;
        case LUA_ERRMEM:
            throw std::runtime_error("Memory allocation error");
            break;
        case LUA_ERRGCMM:
            throw std::runtime_error("Error while running __gc metamethod");
            break;
        case LUA_ERRERR:
            throw std::runtime_error("Error calling error handler");
            break;
        case LUA_OK:
        default:
            break;
        }
    }

    LuaStatus State::getStatus()
    {
        return static_cast<LuaStatus>(lua_status(state));
    }

    State::ReturnValue State::get(Index index)
    {
        return State::ReturnValue(state, index);
    }

    template<> void State::push(bool value)
    {
        lua_pushboolean(state, (value) ? 1 : 0);
    }

    template<> void State::push(int value)
    {
        lua_pushinteger(state, static_cast<lua_Integer>(value));
    }

    template<> void State::push(long value)
    {
        lua_pushinteger(state, static_cast<lua_Integer>(value));
    }

    template<> void State::push(float value)
    {
        lua_pushnumber(state, static_cast<lua_Number>(value));
    }

    template<> void State::push(double value)
    {
        lua_pushnumber(state, static_cast<lua_Number>(value));
    }

    template<> void State::push(const std::string & value)
    {
        lua_pushlstring(state, value.c_str(), value.length());
    }

    template<> void State::push(const char * value)
    {
        lua_pushstring(state, value);
    }

    template<> void State::push(void * value)
    {
        lua_pushlightuserdata(state, value);
    }

    template<typename T>
    void State::pushObject(T && object)
    {
        BaseObjectHelper<T> * rawObject = new BaseObjectHelper<T>(std::move(object));
        void ** internalPointer = reinterpret_cast<void**>(newUserData(sizeof(rawObject)));
        *internalPointer = reinterpret_cast<void*>(rawObject);

        newTable();
        pushFunction(BaseObjectHelper<CFunction>::gcHelper);
        setField(-2, "__gc");

        setMetatable(-2);
    }

    template<typename T>
    void State::pushUnmanagedObject(T && object)
    {
        T * rawObject = new T(std::move(object));
        void ** internalPointer = reinterpret_cast<void**>(newUserData(sizeof(rawObject)));
        *internalPointer = reinterpret_cast<void*>(rawObject);

        newTable();
        pushFunction(BaseObjectHelper<CFunction>::unmanagedGcHelper);
        setField(-2, "__gc");

        setMetatable(-2);
    }

    int State::luaCFunctionWrapper(lua_State * internalState)
    {
        State tmpState{ internalState };
        Index functionIndex = tmpState.upvalueIndex(1);

        if (tmpState.type(functionIndex) == Type::UserData)
        {
            void ** rawPointer =  reinterpret_cast<void**>(static_cast<void*>(tmpState.get(functionIndex)));
            BaseObjectHelper<CFunction> * objectHelper = reinterpret_cast<BaseObjectHelper<CFunction> *>(*rawPointer);
            
            try
            {
                const auto cFunction = objectHelper->getStorage();
                return (*cFunction)(tmpState);
            }
            catch (std::exception & e)
            {
                std::string msg = e.what();
                tmpState.error("Error running a CFunction: %s", msg.c_str());
            }
        }
        else
        {
            tmpState.pop();
        }
        return 0;
    }

    int State::luaUnmanagedCFunctionWrapper(lua_State * internalState)
    {
        State tmpState{ internalState };
        Index functionIndex = tmpState.upvalueIndex(1);

        if (tmpState.type(functionIndex) == Type::UserData)
        {
            void ** rawPointer = reinterpret_cast<void**>(static_cast<void*>(tmpState.get(functionIndex)));
            CFunction * cFunction = reinterpret_cast<CFunction *>(*rawPointer);

            try
            {
                return (*cFunction)(tmpState);
            }
            catch (std::exception & e)
            {
                std::string msg = e.what();
                tmpState.error("Error running a CFunction: %s", msg.c_str());
            }
        }
        else
        {
            tmpState.pop();
        }
        return 0;
    }

    template<> void State::push(State & value)
    {
        lua_pushthread(value.state);
    }

    void State::pushFunction(CFunction && value)
    {
        pushUnmanagedObject(std::move(value));

        lua_pushcclosure(state, &State::luaUnmanagedCFunctionWrapper, 1);
    }

    void State::pushManagedFunction(CFunction && value)
    {
        pushObject(std::move(value));

        lua_pushcclosure(state, &State::luaCFunctionWrapper, 1);
    }

    void State::pushFunction(lua_CFunction value)
    {
        lua_pushcfunction(state, value);
    }

    void State::pushValue(Index index)
    {
        lua_pushvalue(state, index);
    }

    void State::pushGlobalTable()
    {
        lua_pushglobaltable(state);
    }

    void State::pushNil()
    {
        lua_pushnil(state);
    }

    template<typename T>
    auto State::convertToCString(T&& t) const
    {
        if constexpr (std::is_same<std::remove_cv_t<std::remove_reference_t<T>>, std::string>::value)
        {
            return std::forward<T>(t).c_str();
        }
        else
        {
            return std::forward<T>(t);
        }
    }

    template<typename ... Args>
    std::string State::stringFormatInternal(const std::string& format, Args&& ... args) const
    {
#if defined(_WIN32) && defined(_MSC_VER)
        size_t size = snprintf(nullptr, 0, format.c_str(), std::forward<Args>(args) ...) + 1;
#else
        size_t size = snprintf(nullptr, 0, format.c_str(), std::forward<Args>(args) ...) + 1;
#endif
        std::unique_ptr<char[]> buf(new char[size]);
        snprintf(buf.get(), size, format.c_str(), args ...);
        return std::string(buf.get(), buf.get() + size - 1);
    }

    template<typename ... Args>
    std::string State::stringFormat(std::string fmt, Args&& ... args) const
    {
        return stringFormatInternal(fmt, convertToCString(std::forward<Args>(args))...);
    }

    template<typename ...Args>
    void State::push(const std::string & fmt, Args&& ... args)
    {
        std::string buffer{ stringFormat(fmt, std::forward<Args>(args)...) };

        lua_pushstring(state, buffer.c_str());
    }

    void State::pop(size_t count)
    {
        lua_pop(state, static_cast<int>(count));
    }

    int State::absIndex(Index index)
    {
        return lua_absindex(state, index);
    }

    int State::upvalueIndex(Index index)
    {
        return lua_upvalueindex(index);
    }

    void State::arith(Operator luaOperator)
    {
        lua_arith(state, static_cast<int>(luaOperator));
    }

    int State::checkStack(int nSlots)
    {
        return lua_checkstack(state, nSlots);
    }

    CompareResult State::compare(Index leftElement, Index rightElement, Operator luaOperator)
    {
        return static_cast<CompareResult>
            (lua_compare(state, leftElement, rightElement, static_cast<int>(luaOperator)));
    }

    void State::concat(int nElements)
    {
        lua_concat(state, nElements);
    }

    void State::len(Index index)
    {
        lua_len(state, index);
    }

    void State::copy(Index srcElement, Index destElement)
    {
        lua_copy(state, srcElement, destElement);
    }

    void State::replace(Index destIndex)
    {
        lua_replace(state, destIndex);
    }

    void State::remove(Index index)
    {
        lua_remove(state, index);
    }

    void State::rotate(Index startingIndex, int offset)
    {
        lua_rotate(state, startingIndex, offset);
    }

    void State::insert(Index destIndex)
    {
        lua_insert(state, destIndex);
    }

    bool State::next(Index index)
    {
        return
            (
                lua_next(state, index) != 0
            );
    }

    Index State::getTop()
    {
        return lua_gettop(state);
    }

    void State::setTop(Index index)
    {
        lua_settop(state, index);
    }

    void State::xmove(State & destState, int nElements)
    {
        lua_xmove(state, destState.state, nElements);
    }

    Type State::getField(Index tableIndex, const std::string & key)
    {
        return
            static_cast<Type>(
                lua_getfield(state, tableIndex, key.c_str())
            );
    }

    Type State::getGlobal(const std::string & name)
    {
        return
            static_cast<Type>(
                lua_getglobal(state, name.c_str())
                );
    }

    Type State::getI(Index tableIndex, int elementIndex)
    {
        return
            static_cast<Type>(
                lua_geti(state, tableIndex, elementIndex)
                );
    }

    bool State::getMetatable(Index index)
    {
        return
            (
                lua_getmetatable(state, index) == 1
            );
    }

    Type State::getMetatable(const std::string & name)
    {
        return
            static_cast<Type>(
                luaL_getmetatable(state, name.c_str())
                );
    }

    Type State::getTable(Index index)
    {
        return
            static_cast<Type>(
                lua_gettable(state, index)
                );
    }

    Type State::getUserValue(Index index)
    {
        return
            static_cast<Type>(
                lua_getuservalue(state, index)
                );
    }

    void State::setField(Index index, const std::string & key)
    {
        lua_setfield(state, index, key.c_str());
    }

    void State::setGlobal(const std::string & name)
    {
        lua_setglobal(state, name.c_str());
    }

    void State::setI(Index tableIndex, int elementIndex)
    {
        lua_seti(state, tableIndex, elementIndex);
    }

    void State::setMetatable(Index index)
    {
        lua_setmetatable(state, index);
    }

    void State::setMetatable(const std::string & name)
    {
        luaL_setmetatable(state, name.c_str());
    }

    void State::setTable(Index index)
    {
        lua_settable(state, index);
    }

    void State::setUserValue(Index index)
    {
        lua_setuservalue(state, index);
    }

    Type State::type(Index index)
    {
        return
            static_cast<Type>(
                lua_type(state, index)
                );
    }

    std::string State::typeName(Type typeId)
    {
        return std::string(lua_typename(state, static_cast<int>(typeId)));
    }

    void * State::newUserData(size_t size)
    {
        return lua_newuserdata(state, size);
    }

    bool State::newMetatable(const std::string & name)
    {
        return (
            luaL_newmetatable(state, name.c_str()) == 1
            );
    }

    State::ReturnValue::ReturnValue(lua_State * state, Index index)
        : state(state), index(index)
    {
    }

    State::ReturnValue::operator std::string()
    {
        size_t length = 0;
        const char * result = lua_tolstring(state, index, &length);
        return std::string(result, length);
    }

    State::ReturnValue::operator bool()
    {
        return (lua_toboolean(state, index) == 1);
    }

    State::ReturnValue::operator int64_t()
    {
        return static_cast<int64_t>(lua_tointeger(state, index));
    }

    State::ReturnValue::operator long()
    {
        return static_cast<long>(lua_tonumber(state, index));
    }

    State::ReturnValue::operator int()
    {
        return static_cast<int>(lua_tointeger(state, index));
    }

    State::ReturnValue::operator double()
    {
        return static_cast<double>(lua_tonumber(state, index));
    }

    State::ReturnValue::operator float()
    {
        return static_cast<float>(lua_tonumber(state, index));
    }

    State::ReturnValue::operator CFunction()
    {
        lua_CFunction fn = lua_tocfunction(state, index);
        return [&](State & foreignState) -> int
        {
            return fn(foreignState.state);
        };
    }

    State::ReturnValue::operator const void *()
    {
        return lua_topointer(state, index);
    }

    State::ReturnValue::operator void *()
    {
        return lua_touserdata(state, index);
    }

    State::ReturnValue::operator State()
    {
        return State{ lua_tothread(state, index) };
    }

    Type State::ReturnValue::getType()
    {
        int typeId = lua_type(state, index);
        return static_cast<Type>(typeId);
    }

    std::ostream & operator<<(std::ostream & outputStream, State::ReturnValue & rightValue)
    {
        const std::string rightString = rightValue;
        return outputStream << rightString;
    }

    std::string operator+(std::string & leftString, State::ReturnValue & rightValue)
    {
        const std::string rightString = rightValue;
        return leftString + rightString;
    }

    std::string operator+(const char * leftString, State::ReturnValue & rightValue)
    {
        const std::string rightString = rightValue;
        return std::string(leftString) + rightString;
    }


    template<class T>
    State::BaseObjectHelper<T>::BaseObjectHelper(T && object)
    {
        objectStorage = std::make_shared<T>(object);
    }

    template<class T>
    State::BaseObjectHelper<T>::~BaseObjectHelper()
    {
    }
    
    template<class T>
    int State::BaseObjectHelper<T>::gc(State & state)
    {
        return 0;
    }

    template<class T>
    int State::BaseObjectHelper<T>::gcHelper(lua_State * internalState)
    {
        State tmpState{ internalState };
        if (tmpState.type(1) == Type::UserData)
        {
            void ** rawPointer = reinterpret_cast<void**>(static_cast<void*>(tmpState.get(1)));
            BaseObjectHelper<T> * objectPointer = (reinterpret_cast<BaseObjectHelper<T>*>(*rawPointer));
            objectPointer->gc(tmpState);
            delete objectPointer;
        }
        return 0;
    }

    template<class T>
    int State::BaseObjectHelper<T>::unmanagedGcHelper(lua_State * internalState)
    {
        State tmpState{ internalState };
        if (tmpState.type(1) == Type::UserData)
        {
            void ** rawPointer = reinterpret_cast<void**>(static_cast<void*>(tmpState.get(1)));
            T * objectPointer = (reinterpret_cast<T*>(*rawPointer));
            delete objectPointer;
        }
        return 0;
    }

    template<class T>
    std::shared_ptr<T> State::BaseObjectHelper<T>::getStorage() const
    {
        return objectStorage;
    }
}