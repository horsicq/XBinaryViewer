/* Copyright (c) 2019-2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QFileInfo>
#include <QTextStream>

#include "../global.h"
#include "xfmodel_header.h"
#include "xfmodel_table.h"
#include "xformats.h"
#include "xftree_model.h"

namespace {

qint32 printError(const QString &sMessage)
{
    QTextStream(stderr) << sMessage << "\n";
    return 1;
}

QString modelToOutputString(const QAbstractItemModel *pModel, const QString &sFormat, const QString &sTitle)
{
    QString sResult;

    if ((sFormat == "xml") || (sFormat == "json") || (sFormat == "csv") || (sFormat == "tsv")) {
        XFModel::EXPORT_FORMAT exportFormat = XFModel::EXPORT_PLAINTEXT;

        if (sFormat == "xml") {
            exportFormat = XFModel::EXPORT_XML;
        } else if (sFormat == "json") {
            exportFormat = XFModel::EXPORT_JSON;
        } else if (sFormat == "csv") {
            exportFormat = XFModel::EXPORT_CSV;
        } else if (sFormat == "tsv") {
            exportFormat = XFModel::EXPORT_TSV;
        }

        sResult = XFModel::exportToString(pModel, exportFormat);
    } else {
        const XFModel *pXFModel = dynamic_cast<const XFModel *>(pModel);

        if (pXFModel) {
            sResult = pXFModel->modelToString(sTitle);
        }
    }

    return sResult;
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(X_ORGANIZATIONNAME);
    QCoreApplication::setOrganizationDomain(X_ORGANIZATIONDOMAIN);
    QCoreApplication::setApplicationName(X_APPLICATIONNAME);
    QCoreApplication::setApplicationVersion(X_APPLICATIONVERSION);

    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QString("%1 v%2 — console mode: prints a file's structure as a header tree").arg(X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "File to analyze");

    QCommandLineOption formatOption(QStringList() << "f"
                                                   << "format",
                                     "Output format: text (default), formatted, xml, json, csv, tsv", "format", "text");
    parser.addOption(formatOption);

    QCommandLineOption structOption(QStringList() << "S"
                                                  << "struct",
                                    "Show a specific structure/table record, e.g. \"ELF32::ELF_SHDR32?type=TABLE&offset=0x00080af0&size=0xf0&rows=0x06\"",
                                    "struct");
    parser.addOption(structOption);

    parser.process(app);

    const QStringList listArgs = parser.positionalArguments();

    if (listArgs.count() != 1) {
        QTextStream(stderr) << parser.helpText();
        return 1;
    }

    QString sFileName = listArgs.at(0);

    if (!QFileInfo::exists(sFileName)) {
        return printError(QString("File not found: %1").arg(sFileName));
    }

    XBinary::FT fileType = XFormats::getPrefFileType(sFileName, true);

    XFormats::INDATA inData = XFormats::createINDATA(fileType, sFileName);

    QIODevice *pDevice = XFormats::createDevice(inData);

    if (!pDevice) {
        return printError(QString("Cannot open file: %1").arg(sFileName));
    }

    XBinary *pBinary = XFormats::createClass(fileType, pDevice);

    if (!pBinary) {
        XFormats::removeDevice(pDevice, inData);
        return printError(QString("Cannot detect file format: %1").arg(sFileName));
    }

    XBinary::PDSTRUCT pdStruct = XBinary::createPdStruct();
    XBinary::_MEMORY_MAP memoryMap = pBinary->getMemoryMap(XBinary::MAPMODE_UNKNOWN, &pdStruct);
    QString sFormat = parser.value(formatOption).toLower();
    QString sOutput;

    if (parser.isSet(structOption)) {
        QString sStruct = parser.value(structOption);
        XBinary::XFHEADER xfHeader = XFormats::getXFHeaderFromStructName(pDevice, sStruct, inData.bIsImage, inData.nModuleAddress, &pdStruct);

        if (xfHeader.fileType == XBinary::FT_UNKNOWN) {
            delete pBinary;
            XFormats::removeDevice(pDevice, inData);
            return printError(QString("Cannot resolve structure: %1").arg(sStruct));
        }

        QString sTitle = XFormats::getXFHeaderStructName(xfHeader);

        if (xfHeader.xfType == XBinary::XFTYPE_TABLE) {
            XFModel_table model;
            model.setShowPresentation(true);
            model.setData(inData, xfHeader);
            sOutput = modelToOutputString(&model, sFormat, sTitle);
        } else {
            XFModel_header model;
            model.setData(inData, xfHeader);
            sOutput = modelToOutputString(&model, sFormat, sTitle);
        }

        QTextStream(stdout) << sOutput << "\n";

        delete pBinary;
        XFormats::removeDevice(pDevice, inData);

        return 0;
    }

    XBinary::XLOC xLoc = {};
    xLoc.locType = XBinary::LT_OFFSET;
    xLoc.nLocation = 0;

    XBinary::XFSTRUCT xfStruct = {};
    xfStruct.pMemoryMap = &memoryMap;
    xfStruct.fileType = memoryMap.fileType;
    xfStruct.nStructID = 0;
    xfStruct.xLoc = xLoc;
    xfStruct.xfType = XBinary::XFTYPE_HEADER;
    xfStruct.bIsParent = true;
    xfStruct.nCount = -1;

    QList<XBinary::XFHEADER> listHeaders = pBinary->getXFHeaders(xfStruct, &pdStruct);

    XFTreeModel treeModel;
    treeModel.setData(inData, listHeaders);

    if (sFormat == "xml") {
        sOutput = treeModel.toXML();
    } else if (sFormat == "json") {
        sOutput = treeModel.toJSON();
    } else if (sFormat == "csv") {
        sOutput = treeModel.toCSV();
    } else if (sFormat == "tsv") {
        sOutput = treeModel.toTSV();
    } else if (sFormat == "formatted") {
        sOutput = treeModel.toFormattedString();
    } else {
        sOutput = XFTreeModel::treeToString(&treeModel, sFileName);
    }

    QTextStream(stdout) << sOutput << "\n";

    delete pBinary;
    XFormats::removeDevice(pDevice, inData);

    return 0;
}
