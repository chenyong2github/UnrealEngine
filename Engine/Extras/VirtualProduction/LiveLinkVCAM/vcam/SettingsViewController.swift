//
//  SettingsViewController.swift
//  Live Link VCAM
//
//  Created by Brian Smith on 12/11/19.
//  Copyright Epic Games, Inc. All Rights Reserved.
//


import UIKit

class SettingsViewController : UITableViewController {
    
    let appSettings = AppSettings.shared
    
    override func viewDidLoad() {
        super.viewDidLoad()
        if UIDevice.current.userInterfaceIdiom == .phone {
            self.navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .done, target: self, action: #selector(done))
        }
    }
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        self.tableView.reloadData()
    }

    override func tableView(_ tableView: UITableView, willDisplay cell: UITableViewCell, forRowAt indexPath: IndexPath) {
        
        if let detail = cell.detailTextLabel {
            detail.textColor = UIColor.secondaryLabel
        }
        
        switch cell.reuseIdentifier {

        case "liveLink":
            cell.detailTextLabel?.text = appSettings.liveLinkSubjectName

        case "timecode":
            cell.detailTextLabel?.text = Timecode.sourceToString(appSettings.timecodeSourceEnum())

        default:
            break
        }
    }
    
    @objc func done(sender:Any?) {
        self.navigationController?.dismiss(animated: true, completion: nil)
    }

}
