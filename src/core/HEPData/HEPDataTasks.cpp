/* **********************************************************************
 * Copyright (C) 2019-2026, Claude Pruneau, Victor Gonzalez, Oveis Sheibani
 * All rights reserved.
 *
 * For the licensing terms see LICENSE.
 *
 * Author: Claude Pruneau
 *
 * *********************************************************************/
//! @file HEPDataTasks.cpp
//! @brief Task classes of the HEPData library, registered for Task::instantiateTask() (see TaskRegistration.hpp).
#include "HEPDataExporter.hpp"
#include "TaskRegistration.hpp"

DATUM_REFLECTION(HEPDataTasks)
{
  Datum::registerTask<Datum::HEPDataExporter>("Datum::HEPDataExporter");
}
