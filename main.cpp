
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {

std::list<int> l;                   // Спільний глобальний список (1.2.3–1.2.5)
std::mutex listMutex;              // М'ютекс, що захищає l
std::mutex outputMutex;            // Незалежний м'ютекс для цілісного виводу

void logLine(const std::string& text) {
    std::lock_guard<std::mutex> lock(outputMutex);
    std::cout << text << '\n';
}

void Thread1() { std::cout << '1' << std::flush; }
void Thread2() { std::cout << '2' << std::flush; }

// 1.2.1.
void task1() {
    std::set_terminate([] {
        std::cerr << "\nstd::terminate: потік знищено без join() або detach().\n";
        std::_Exit(42);
    });
    std::thread first(Thread1);
    std::thread second(Thread2);

}

// 1.2.2. Від'єднані потоки не змушують main() очікувати їх завершення.
void task2() {
    std::thread first(Thread1);
    std::thread second(Thread2);
    first.detach();
    second.detach();

}

// 1.2.3.
void AddToListUnsafe(int start) {
    for (int i = 0; i < 10; ++i) {
        const int value = start + i;
        l.push_back(value);
        logLine("[add без м'ютекса] " + std::to_string(value));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void ListContainsUnsafe(int target) {
    for (int i = 0; i < 10; ++i) {
        const bool found = std::find(l.begin(), l.end(), target) != l.end();
        logLine("[find без м'ютекса] спроба " + std::to_string(i + 1) +
                ": " + (found ? "знайдено" : "не знайдено"));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void task3(int start, int target) {
    l.clear();
    std::thread add(AddToListUnsafe, start);
    std::thread check(ListContainsUnsafe, target);
    add.join();
    check.join();
    logLine("[1.2.3] Розмір списку: " + std::to_string(l.size()) +
            "; результат не гарантується через data race.");
}

// 1.2.4.
void AddToListMutex(int start) {
    for (int i = 0; i < 10; ++i) {
        const int value = start + i;
        listMutex.lock();
        l.push_back(value);
        listMutex.unlock();
        logLine("[add mutex] " + std::to_string(value));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void ListContainsMutex(int target) {
    for (int i = 0; i < 10; ++i) {
        listMutex.lock();
        const bool found = std::find(l.begin(), l.end(), target) != l.end();
        listMutex.unlock();
        logLine("[find mutex] спроба " + std::to_string(i + 1) +
                ": " + (found ? "знайдено" : "не знайдено"));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void task4(int start, int target) {
    l.clear();
    std::thread add(AddToListMutex, start);
    std::thread check(ListContainsMutex, target);
    add.join();
    check.join();
    logLine("[1.2.4] Розмір списку: " + std::to_string(l.size()));
}

struct Completion {
    std::mutex mutex;
    std::condition_variable cv;
    int count = 0;

    void done() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++count;
        }
        cv.notify_one();
    }

    void waitFor(int expected) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return count == expected; });
    }
};

// 1.2.5.
void AddToListOnce(int value) {
    {
        std::lock_guard<std::mutex> lock(listMutex);
        l.push_back(value);
    }
    logLine("[add guard] " + std::to_string(value));
}

void ListContainsOnce(int target) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(listMutex);
        found = std::find(l.begin(), l.end(), target) != l.end();
    }
    logLine("[find guard] " + std::to_string(target) + ": " +
            (found ? "знайдено" : "не знайдено"));
}

void task5(int start, int target) {
    l.clear();
    auto completed = std::make_shared<Completion>();
    for (int i = 0; i < 10; ++i) {
        const int value = start + i;
        std::thread([value, completed] {
            AddToListOnce(value);
            completed->done();
        }).detach();
        std::thread([target, completed] {
            ListContainsOnce(target);
            completed->done();
        }).detach();
    }
    
    completed->waitFor(20);
    logLine("[1.2.5] Завершено 20 від'єднаних потоків; розмір списку: " +
            std::to_string(l.size()));
}

struct someData {
    std::string name;
    std::string surname;
    std::string address;
    int age;
};

std::string describe(const someData& p) {
    return p.name + " " + p.surname + ", адреса=" + p.address +
           ", вік=" + std::to_string(p.age);
}

class exchangePerson {
    someData person_;
    mutable std::mutex mutex_;

public:
    explicit exchangePerson(someData initial) : person_(std::move(initial)) {}

    static void JohnDoe(exchangePerson& object) {
        std::lock_guard<std::mutex> lock(object.mutex_);
        object.person_ = {"John", "Doe", "Unknown", 120};
    }

    static void JacobSmith(exchangePerson& object) {
        std::lock_guard<std::mutex> lock(object.mutex_);
        object.person_ = {"Jacob", "Smith", "Known", 1};
    }

    someData snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return person_;
    }

    // 1.2.6.
    static void Swap(exchangePerson& first, exchangePerson& second) {
        if (&first == &second) {
            logLine("Swap: обидва посилання вказують на той самий об'єкт.");
            return;
        }
        std::lock(first.mutex_, second.mutex_);
        std::lock_guard<std::mutex> guard1(first.mutex_, std::adopt_lock);
        std::lock_guard<std::mutex> guard2(second.mutex_, std::adopt_lock);
        logLine("[Swap guard] ДО:    A=" + describe(first.person_) +
                " | B=" + describe(second.person_));
        std::swap(first.person_, second.person_);
        logLine("[Swap guard] ПІСЛЯ: A=" + describe(first.person_) +
                " | B=" + describe(second.person_));
    }

    // 1.2.7.
    static void SwapUnique(exchangePerson& first, exchangePerson& second) {
        if (&first == &second) {
            logLine("SwapUnique: обидва посилання вказують на той самий об'єкт.");
            return;
        }
        std::unique_lock<std::mutex> lock1(first.mutex_, std::defer_lock);
        std::unique_lock<std::mutex> lock2(second.mutex_, std::defer_lock);
        std::lock(lock1, lock2);
        logLine("[Swap unique] ДО:    A=" + describe(first.person_) +
                " | B=" + describe(second.person_));
        std::swap(first.person_, second.person_);
        logLine("[Swap unique] ПІСЛЯ: A=" + describe(first.person_) +
                " | B=" + describe(second.person_));
    }
};

void runExchange(bool useUnique) {
    exchangePerson a({"Initial", "A", "First", 20});
    exchangePerson b({"Initial", "B", "Second", 30});
    auto completed = std::make_shared<Completion>();


    auto launchDetached = [&completed](auto function, exchangePerson& person) {
        std::thread([&person, function, completed] {
            function(person);
            completed->done();
        }).detach();
    };
    launchDetached(exchangePerson::JohnDoe, a);
    launchDetached(exchangePerson::JacobSmith, a);
    launchDetached(exchangePerson::JohnDoe, b);
    launchDetached(exchangePerson::JacobSmith, b);

 
    std::thread swapThread([&] {
        if (useUnique) {
            exchangePerson::SwapUnique(a, b);
        } else {
            exchangePerson::Swap(a, b);
        }
    });
    swapThread.join();
    completed->waitFor(4);

    logLine("[підсумок] A=" + describe(a.snapshot()));
    logLine("[підсумок] B=" + describe(b.snapshot()));
}

void task8() {
    logLine("1.2.8: однаковий порядок захоплення або std::lock/scoped_lock ");
    logLine("знижують ризик deadlock; мінімізуйте критичні секції та");
    logLine("не робіть вкладених несумісних блокувань.");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Використання: " << argv[0] << " <номер 1..8>\n"
                  << "1 — очікуваний std::terminate (код виходу 42); "
                  << "2 — detach; 3 — data race; 4 — mutex;\n"
                  << "5 — lock_guard/detach; 6 — adopt_lock; "
                  << "7 — unique_lock; 8 — висновок.\n";
        return 1;
    }

    const int start = 10;
    const int target = 14;
    const std::string task = argv[1];
    if (task == "1") task1();
    else if (task == "2") task2();
    else if (task == "3") task3(start, target);
    else if (task == "4") task4(start, target);
    else if (task == "5") task5(start, target);
    else if (task == "6") runExchange(false);
    else if (task == "7") runExchange(true);
    else if (task == "8") task8();
    else {
        std::cerr << "Невідоме завдання: " << task << '\n';
        return 1;
    }
    return 0;
}

