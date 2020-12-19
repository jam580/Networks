#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

//Can be run with -I/usr/include/python3.8
#include <Python.h>
#include <stdlib.h>

void * launchServe()
{
    //temp is python filename
    //method is specific funciton inside of python code we wish to call
    char * temp = "newtemp";
    char * method = "launch";
    //set the python environment
    setenv("PYTHONPATH", ".", 0);

    //must be called before all other Py functions
    Py_Initialize();

    PyObject *pValue;
    PyObject *name = PyUnicode_DecodeFSDefault(temp);
    //attempt to load the python program as a module
    PyObject *pModule = PyImport_Import(name);
    Py_DECREF(name);

    if(pModule!=NULL)
    {
        //load the specific function from the python program
        PyObject *pFunc = PyObject_GetAttrString(pModule, method);
        //check if program is callable and exists
        if(pFunc && PyCallable_Check(pFunc))
        {

            printf("Yeehaw\n");
            //run the function in question, store return value
            pValue = PyObject_CallObject(pFunc,NULL);

            //print out a value if we got it
            if (pValue != NULL) {
                printf("Result of call: %ld\n", PyLong_AsLong(pValue));
                Py_DECREF(pValue);
            }
            //error occured
            else {
                Py_DECREF(pFunc);
                Py_DECREF(pModule);
                PyErr_Print();
                fprintf(stderr,"Call failed\n");
                return NULL;
            }
        } //if function exists and is callable
        else { 
            if (PyErr_Occurred())
                PyErr_Print();
            fprintf(stderr, "Cannot find function launch \n");
        }
        Py_XDECREF(pFunc);
        Py_DECREF(pModule);
    }//if pmodule != NULL
    else { //failed to find python source code
        PyErr_Print();
        fprintf(stderr, "Failed to load \"%s\"\n", temp);
        return NULL;
    }
    if(Py_FinalizeEx() < 0)
        exit(120);
}

//gcc -I/usr/include/python3.8 webpage.c -lpython3.8 -lpthread
int main(int argc, char **argv) {

    pthread_t id;
    char input;
    while(1)
    {
        //when you hit enter it gets pulled as well
        if(input!='\n')
            printf("s to start, q to try and quit\n");

        input = getchar();

        if(input == 's')
        {
            
            pthread_create(&id, NULL, launchServe, NULL);
            printf("yoho we just launched a server\n");
        }
        else if(input =='q')
        {
            printf("Thats cute you tried to quit\n");
            pthread_cancel(id);
        }
        else if(input=='z')
        {
            break;
        }                       
    }

    return 0;
}
