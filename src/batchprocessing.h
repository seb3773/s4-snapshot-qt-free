/**********************************************************************
 *  batchprocessing.h
 **********************************************************************
 * Copyright (C) 2020-2025 MX Authors
 *
 * Authors: Adrian
 *          Debian <http://debian.org>
 *
 * This file is part of S4 Snapshot.
 *
 * S4 Snapshot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * S4 Snapshot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with S4 Snapshot.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#pragma once

#include <QString>

#include "settings.h"
#include "work.h"

class Batchprocessing
{
public:
    explicit Batchprocessing(Settings *settings);

#ifdef UNIT_TESTS
    explicit Batchprocessing(Settings *settings, bool unitTestNoRun);
    void ut_checkUpdatedDefaultExcludesCliWithIo(FILE *in, FILE *out) { checkUpdatedDefaultExcludesCliWithIo(in, out); }
#endif

#ifdef UNIT_TESTS
    [[nodiscard]] static QString ut_colorizeDiffAnsi(const QString &diff) { return colorizeDiffAnsi(diff); }
#endif

private:
    Settings *settings;
    Work work;

    void checkUpdatedDefaultExcludesCli();
    void checkUpdatedDefaultExcludesCliWithIo(FILE *in, FILE *out);
    void checkNvidiaGraphicsCard();
    [[nodiscard]] static QString colorizeDiffAnsi(const QString &diff);
    [[nodiscard]] bool isSourceExcludesNewer(QString &diffOutput) const;
    [[nodiscard]] bool resetCustomExcludesCli(const QString &configuredPath, const QString &sourcePath) const;
};
