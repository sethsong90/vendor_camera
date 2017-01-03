__copyright__ = '''
Copyright (c) 2016 Qualcomm Technologies, Inc.
All Rights Reserved.
Confidential and Proprietary - Qualcomm Technologies, Inc.
'''


import xml.etree.ElementTree
import sys
import time
from optparse import OptionParser

class ProcessScopePacketTypeFile:

    def __init__(self):
        use = 'usage: %prog [options] arg1 arg2'
        desc = ("arg1: input xml file "
                "arg2: output directory for header file"
        )
        parser = OptionParser(usage=use, description=desc)
        (options, args) = parser.parse_args()
        if len(args) != 2:
            parser.error("incorrect number of arguments, need 2 arguments")
        self.inputXMLFilePath = str(args[0])
        self.outputHeaderFilePath = str(args[1])
        self.inputXML = 0
        self.outputHeaderFile = 0

    def openFiles(self):
        self.inputXML = (
            xml.etree.ElementTree.parse(self.inputXMLFilePath).getroot())
        self.outputHeaderFile = open(str(self.outputHeaderFilePath) +
                                     '/camscope_packet_type.h', 'w')

    def closeFiles(self):
        self.outputHeaderFile.close()

    def run(self):
        self.openFiles()
        current_year = time.strftime('%Y')
        year_str = ""
        if current_year != '2016':
             year_str = "-" + current_year
        copyrightString = (
        "//****************************************************************\n"
        "// Copyright (c) 2016" + year_str + " Qualcomm Technologies, Inc.\n"
        "// All Rights Reserved.\n"
        "// Confidential and Proprietary - Qualcomm Technologies, Inc.\n"
        "//\n"
        "//****************************************************************\n"
        "\n"
        "/*\n"
        " *****************************************************************\n"
        " * @file  camscope_packet_type.h\n"
        " * @brief CameraScope Packet Types\n"
        " *        <<DO NOT EDIT:  Created at build time>>\n"
        " *            See camscope_packet_type.xml for changes\n"
        " *****************************************************************\n"
        " */\n")
        self.outputHeaderFile.write(copyrightString)
        self.outputHeaderFile.write('#ifndef __CAMSCOPE_PACKET_TYPE_H__\n'
                                    '#define __CAMSCOPE_PACKET_TYPE_H__\n')

        self.outputHeaderFile.write('#include <stdint.h>\n');
        self.outputHeaderFile.write('#include <time.h>\n');

        #Write camscope_packet_type enum
        for enumType in self.inputXML.findall('enum'):
            self.outputHeaderFile.write('typedef enum {\n')
            for type in enumType.findall('enumVal'):
                self.outputHeaderFile.write('    ' + type.get('Value') +
                                            ' = ' + type.get('Number') +
                                            ',\n')
            self.outputHeaderFile.write('} ' + enumType.get('Name') + ';\n\n')
        #Write camscope structs
        for struct in self.inputXML.findall('struct'):
            self.outputHeaderFile.write('typedef struct {\n')
            for variable in struct.findall('var'):
                self.outputHeaderFile.write('    ' + variable.get('Type') +
                                            ' ' + variable.get('Name') +
                                            ';\n')
            self.outputHeaderFile.write('} ' + struct.get('Name') + ';\n\n')

        self.outputHeaderFile.write('#endif')
        self.closeFiles()


if __name__ == "__main__":
    scope = ProcessScopePacketTypeFile()
    scope.run()
