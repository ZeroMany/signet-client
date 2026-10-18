#ifndef ABOUT_H
#define ABOUT_H

#include <QDialog>

class About : public QDialog
{
	Q_OBJECT
public:
	About(bool connectedDevice, QWidget *parent);
};

#endif // ABOUT_H
