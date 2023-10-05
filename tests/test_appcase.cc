#include "yhchaos/appcase.h"

int main(int argc, char** argv) {
    yhchaos::AppCase app;
    if(app.init(argc, argv)) {
        return app.run();
    }
    return 0;
}
