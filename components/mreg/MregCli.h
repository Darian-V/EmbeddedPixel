#pragma once

#include "MregSupervisor.h"
#include "MregTrajectoryPlanner.h"
#include <cstddef>

namespace app::mreg {

class MregCli {
public:
    MregCli(MregSupervisor& supervisor, MregTrajectoryPlanner& planner);

    int execute(const char* cmd, char* out_buf, size_t max_len);

private:
    MregSupervisor&        supervisor_;
    MregTrajectoryPlanner& planner_;

    void print_status(char* out, size_t max_len);
    void print_help(char* out, size_t max_len);
};

} // namespace app::mreg
